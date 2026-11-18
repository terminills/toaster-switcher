/* The project in the switcher: its file and croutons (see swproject.h). */
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdlib.h>
#include <string.h>
#include "swproject.h"

uint8_t *readfile(const char *name, LONG *len)
{
    BPTR fh = Open((CONST_STRPTR)name, MODE_OLDFILE);
    uint8_t *buf = NULL;
    LONG n;
    if (!fh) return NULL;
    Seek(fh, 0, OFFSET_END);
    n = Seek(fh, 0, OFFSET_BEGINNING);
    if (n > 0 && (buf = malloc(n)) && Read(fh, buf, n) != n) {
        free(buf);
        buf = NULL;
    }
    Close(fh);
    *len = n;
    return buf;
}

/* The project: its file, header and croutons (sw->fx holds up to MAXFX). */
char projpath[256] = "Toaster:Programs/Switcher_Support/DefaultProject";
static uint8_t *projhdr;
static size_t projhdrlen;

const char *base_name(const char *path)
{
    const char *b = strrchr(path, '/');
    if (!b) b = strrchr(path, ':');
    return b ? b + 1 : path;
}

/* Open the crouton at path into sw->fx[at] (later ones move up), with its project entry (a new one when NULL).
 * 0 on success. */
int add_crouton(struct switcher *sw, int at, const char *path, const uint8_t *entry, size_t entrylen, int *ovl)
{
    static const char *const tname[] = { "ANIM", "ANIM", "ALGO", "ILBM", "MAIN", "CTRL", "REXX" };
    struct effect e;
    LONG fl;
    uint8_t *fb;
    if (sw->nfx >= MAXFX || at < 0 || at > sw->nfx || !(fb = readfile(path, &fl))) return -1;
    if (effect_open(&e, base_name(path), fb, fl)) {
        free(fb);
        return -1;
    }
    effect_note_path(&e, path);
    if (e.type == FX_OTHER || !e.has_icon) {        /* CrUD and icon may be only in the .i file */
        char ipath[260];
        LONG il;
        uint8_t *ib;
        strncpy(ipath, path, sizeof(ipath) - 3);
        ipath[sizeof(ipath) - 3] = 0;
        strcat(ipath, ".i");
        if ((ib = readfile(ipath, &il))) {
            effect_merge_icon(&e, ib, il);
            free(ib);
        }
    }
    if (entry && (e.entry = malloc(entrylen))) {
        memcpy(e.entry, entry, entrylen);
        e.entrylen = entrylen;
    } else if (!entry) {
        e.entry = project_entry_new(path, tname[e.type < 7 ? e.type : 0], &e.entrylen);
    }
    effect_entry_controls(&e);
    memmove(&sw->fx[at + 1], &sw->fx[at], (sw->nfx - at) * sizeof(e));
    sw->fx[at] = e;
    sw->nfx++;
    if (sw->fxsel >= at) sw->fxsel++;
    if (ovl && *ovl >= at) (*ovl)++;
    return 0;
}

int load_project(struct switcher *sw, const char *project)
{
    static char names[MAXFX][256];
    static size_t off[MAXFX];
    LONG n;
    uint8_t *pj;
    int k, i;
    if (!sw->fx && !(sw->fx = calloc(MAXFX, sizeof(struct effect)))) return -1;
    if (!(pj = readfile(project, &n))) return -1;
    if ((k = project_entries(pj, n, names, off, MAXFX)) < 0) {
        free(pj);
        return -1;
    }
    for (i = 0; i < sw->nfx; i++) {
        effect_close(&sw->fx[i]);
        free((void *)sw->fx[i].file);
    }
    sw->nfx = sw->fxtop = 0;
    sw->fxsel = -1;
    free(projhdr);
    projhdr = NULL;
    strncpy(projpath, project, sizeof(projpath) - 1);
    if ((projhdrlen = project_header(pj, n)) && (projhdr = malloc(projhdrlen))) memcpy(projhdr, pj, projhdrlen);
    for (i = 0; i < k; i++) {
        uint32_t size = (uint32_t)pj[off[i]] << 24 | pj[off[i] + 1] << 16 | pj[off[i] + 2] << 8 | pj[off[i] + 3];
        add_crouton(sw, sw->nfx, names[i], pj + off[i], 4 + size, NULL);
    }
    free(pj);
    return 0;
}

int save_project_to(struct switcher *sw, const char *path)
{
    uint8_t *buf;
    size_t n;
    BPTR fh;
    int rc = -1;
    if (project_build(projhdr, projhdrlen, sw->fx, sw->nfx, &buf, &n)) return -1;
    if ((fh = Open((CONST_STRPTR)path, MODE_NEWFILE))) {
        if (Write(fh, buf, n) == (LONG)n) {
            strncpy(projpath, path, sizeof(projpath) - 1);
            projpath[sizeof(projpath) - 1] = 0;
            rc = 0;
        }
        Close(fh);
    }
    free(buf);
    return rc;
}

void select_effect(struct switcher *sw, int i, int onair)
{
    if (i == sw->fxsel || i < 0 || i >= sw->nfx) return;
    if (sw->fxsel >= 0 && sw->fxsel != onair) effect_free_frames(&sw->fx[sw->fxsel]);
    sw->fxsel = i;
    if (sw->fx[i].ctl_speed >= 0) sw->speed = sw->fx[i].ctl_speed;     /* its own speed, from Controls */
    effect_frames(&sw->fx[i]);      /* wipe frames, picture or ALGO parameters; what is not understood dissolves */
}
/* The selected overlay finished fading: off when it was on air, otherwise on air in place of the previous one. */
int overlay_done(struct switcher *sw, int onair)
{
    if (sw->fxsel == onair) return -1;
    if (onair >= 0) effect_free_frames(&sw->fx[onair]);
    return sw->fxsel;
}
/* The Editor: croutons in and out of the project, and the project saved. */
void show_selected(struct switcher *sw)
{
    int row = sw->fxsel / GRID_COLS;
    if (sw->fxsel < 0) return;
    if (row < sw->fxtop) sw->fxtop = row;
    if (row >= sw->fxtop + GRID_ROWS) sw->fxtop = row - GRID_ROWS + 1;
}

void delete_crouton(struct switcher *sw, int i, int *ovl)
{
    if (i < 0 || i >= sw->nfx) return;
    if (*ovl == i) *ovl = -1;
    else if (*ovl > i) (*ovl)--;
    effect_close(&sw->fx[i]);
    free((void *)sw->fx[i].file);
    memmove(&sw->fx[i], &sw->fx[i + 1], (sw->nfx - i - 1) * sizeof(struct effect));
    sw->nfx--;
    sw->fxsel = -1;
    select_effect(sw, i < sw->nfx ? i : sw->nfx - 1, *ovl);
    if (sw->fxtop && sw->fxtop * GRID_COLS >= sw->nfx) sw->fxtop--;
}

void clone_crouton(struct switcher *sw, int i, int *ovl)
{
    char path[256];
    if (i < 0 || i >= sw->nfx) return;
    strcpy(path, sw->fx[i].path);
    if (!add_crouton(sw, i + 1, path, sw->fx[i].entry, sw->fx[i].entrylen, ovl)) {
        select_effect(sw, i + 1, *ovl);
        show_selected(sw);
    }
}
