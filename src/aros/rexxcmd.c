/* The Switcher's ARexx function (see rexxcmd.h). */
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <rexx/errors.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rexxcmd.h"
#include "cgprog.h"
#include "swproject.h"
#include "gfxfile.h"
#include "effect.h"

#define FRAMES 30               /* the Toaster counts NTSC frames */
#define TICKS 50

static int border, terminators;

static int is(const char *s, const char *cmd)
{
    int i;
    for (i = 0; i < 4; i++)
        if (toupper((unsigned char)s[i]) != cmd[i]) return 0;
    return 1;
}

/* A decimal number, all of s. */
static int number(const char *s, long *out)
{
    char *end;
    while (*s == ' ') s++;
    if (!*s) return 0;
    *out = strtol(s, &end, 10);
    while (*end == ' ') end++;
    return !*end;
}

/* A bus button: M001-M004, MDV1, MDV2, MBKG and the same for P and O. */
static int bus_slot(const char *s)
{
    static const char *const names[NSLOTS] = { "001", "002", "003", "004", "DV1", "DV2", "BKG" };
    int i, k;
    for (i = 0; i < NSLOTS; i++) {
        for (k = 0; k < 3 && toupper((unsigned char)s[1 + k]) == names[i][k]; k++)
            ;
        if (k == 3) return i;
    }
    return -1;
}

/* Preview's frame buffer is DV1's: the still held there, or the frame it shows. */
static const uint32_t *frame_buffer(const struct rxstate *st)
{
    const struct framestore *fs = st->fs;
    if (st->sw->row[ROW_PRVW].hilite == SLOT_DV2) return NULL;
    if (fs->frozen && fs->still[fs->cur]) return fs->still[fs->cur];
    return inputs_grab(st->in, SLOT_DV1, st->dvbuf) ? NULL : st->dvbuf;
}

static int save_frame(const struct rxstate *st, const char *name)
{
    const uint32_t *rgb = frame_buffer(st);
    uint8_t *buf;
    size_t n;
    BPTR fh;
    int rc = -1;
    if (!rgb || !*name || ilbm_encode24(rgb, st->fs->w, st->fs->h, &buf, &n)) return -1;
    if ((fh = Open((CONST_STRPTR)name, MODE_NEWFILE))) {
        if (Write(fh, buf, n) == (LONG)n) rc = 0;
        Close(fh);
    }
    free(buf);
    return rc;
}

/* A picture into the current still, which DV1 then holds: an ILBM, a frame or key page of the CG, or a framestore
 * file of the Toaster. */
static int load_frame(const struct rxstate *st, const char *name)
{
    struct framestore *fs = st->fs;
    struct picture pic;
    LONG len;
    uint8_t *fb;
    int rc = -1;
    if (st->sw->row[ROW_PRVW].hilite == SLOT_DV2 || !(fb = readfile(name, &len))) return -1;
    if (!ilbm_decode(fb, len, &pic)) {
        if (!fs_load(fs, fs->cur, pic.rgb, pic.w, pic.h)) {
            fs->frozen = 1;
            rc = 0;
        }
        picture_free(&pic);
    } else {                            /* a page of the CG, or a framestore file of the Toaster */
        struct cgpage pg;
        uint32_t *rgb;
        if (!cgpage_decode(&pg, fb, len)) {
            if (pg.w == CGPAGE_W && pg.h == CGPAGE_H && (rgb = cgpage_frame(&pg, fs->w, fs->h))) {
                fs_page(fs, rgb, 1);
                rc = fs->frozen ? 0 : -1;
            }
            cgpage_free(&pg);
        }
    }
    free(fb);
    return rc;
}

static int status(const struct rxstate *st, const char *what, char *text, size_t size)
{
    const struct switcher *sw = st->sw;
    const struct effect *fx = FXCUR(sw);
    long v;
    if (is(what, "MAIN")) v = 1L << sw->row[ROW_MAIN].hilite;
    else if (is(what, "PREV")) v = 1L << sw->row[ROW_PRVW].hilite;
    else if (is(what, "OVLY")) v = sw->row[ROW_LUMKEY].hilite >= 0 ? 1L << sw->row[ROW_LUMKEY].hilite : 0;
    else if (is(what, "FREZ")) v = st->fs->frozen ? 3 : 0;
    else if (is(what, "TBAR")) v = ((st->tbdown ? st->mix : st->mixmax - st->mix) * 511L + st->mixmax / 2) / st->mixmax;
    else if (is(what, "CLIP")) v = sw->keyclip;
    else if (is(what, "SPED")) v = sw->speed == 0 ? 3 : sw->speed - 1;      /* M 0, F 1, S 3 */
    else if (is(what, "FCNT"))
        v = (fx ? effect_ticks(fx, sw->speed, st->speedticks[sw->speed]) : st->speedticks[sw->speed]) * FRAMES / TICKS;
    else if (is(what, "KNUM")) v = sw->fxsel >= 0 ? sw->fxsel : 0;
    else if (is(what, "BACK")) v = sw->bgcolor - 1;
    else if (is(what, "BORD")) v = border;
    else if (is(what, "TERM")) v = terminators;
    else if (is(what, "FACE")) v = 2;
    else if (is(what, "GRID") || is(what, "BANK")) v = 0;
    else {
        const char *s;
        if (is(what, "KEYM")) {
            int k = sw->row[ROW_CLIP].hilite;
            s = k == CLIP_BLACK ? "BLACK" : k == CLIP_WHITE ? "WHITE" : "OFF";
        } else if (is(what, "FXNM")) s = fx ? fx->name : "";
        else if (is(what, "TEXT")) s = fx ? fx->name : "";
        else if (is(what, "INFO")) s = "FX";
        else if (is(what, "SGPI")) s = "OFF";
        else return -1;
        snprintf(text, is(what, "TEXT") && size > 25 ? 25 : size, "%s", s);
        return 0;
    }
    snprintf(text, size, "%ld", v);
    return 0;
}

void rxswitcher(const struct rxstate *st, const char *fn, int argc, const char *const *argv, struct rxout *o)
{
    static const char *const removed[] = { "PAGE", "BKLD", "CHGR", "CKGD", "RDGD", "WTGD", "PJLD", "PJCK", "PJDV",
        "PJSV", "PJDL", "PJRN", "PJXI", "PJNM", "PJMK", "PJBD", "FSDV", "FSNM", "FSMK", "FSCK", "FSBD", "FSCT", "FMDL",
        "FMRN", "FMXI", "KEYP", "KEYN" };
    /* nothing to do for them here: obsolete ones, GPI output with GPI off, what only saved the Amiga time */
    static const char *const nops[] = { "NOPR", "GOCG", "GOFX", "GOLD", "GOSA", "DOEN", "DISW", "ENSW", "DIIM", "ENIM",
        "OGPI", "GRON", "STTP", "DPTP", "DPLW" };
    struct switcher *sw = st->sw;
    struct framestore *fs = st->fs;
    const char *c, *a1 = argc > 1 ? argv[1] : "", *a2 = argc > 2 ? argv[2] : "";
    unsigned i;
    long n;
    int slot;

    memset(o, 0, sizeof(*o));
    strcpy(o->text, "OK");
    if (strlen(fn) != 8 || !is(fn, "SWIT") || !is(fn + 4, "CHER")) {    /* another library's function */
        o->rc = RC_WARN;
        o->rc2 = ERR10_001;
        return;
    }
    o->rc = RC_ERROR;
    if (argc < 1 || strlen(c = argv[0]) < 4) return;
    for (i = 0; i < sizeof(removed) / sizeof(removed[0]); i++)
        if (is(c, removed[i])) return;
    o->rc = RC_OK;
    for (i = 0; i < sizeof(nops) / sizeof(nops[0]); i++)
        if (is(c, nops[i])) return;

    if (is(c, "TAKE") || is(c, "AUTO")) {
        o->act = RXA_PRESS;
        o->row = ROW_TRANS;
        o->index = is(c, "TAKE") ? TRANS_TAKE : TRANS_AUTO;
    } else if (is(c, "UATO")) {
        o->act = RXA_UATO;
    } else if (is(c, "TBAR") && number(a1, &n) && n >= 0 && n <= 511) {
        o->act = RXA_TBAR;
        o->value = ((st->tbdown ? n : 511 - n) * st->mixmax + 255) / 511;
    } else if (is(c, "SLOW") || is(c, "MEDM") || is(c, "FAST")) {
        sw->speed = is(c, "SLOW") ? 0 : is(c, "MEDM") ? 1 : 2;
        o->act = RXA_CHANGED;
    } else if (is(c, "FRES")) {
        *st->framebase = st->now;
    } else if (is(c, "WAIT") && number(a1, &n) && n >= 0) {
        o->until = *st->framebase + n * TICKS / FRAMES;
    } else if (is(c, "WAIT") && is(a1, "GPI")) {
        ;                                       /* GPI triggers are off */
    } else if (is(c, "CLIP") && number(a1, &n) && n >= 0 && n <= 257) {
        sw->keyclip = n > 255 ? 255 : n;
        o->act = RXA_CHANGED;
    } else if (is(c, "KEYM") || is(c, "KOFF") || is(c, "KBLK") || is(c, "KWHT")) {
        /* KEYM: from black to white and back, and from off to black */
        sw->row[ROW_CLIP].hilite = is(c, "KOFF") ? CLIP_OFF : is(c, "KBLK") ? CLIP_BLACK : is(c, "KWHT") ? CLIP_WHITE :
                                   sw->row[ROW_CLIP].hilite == CLIP_BLACK ? CLIP_WHITE : CLIP_BLACK;
        o->act = RXA_CHANGED;
    } else if (strchr("MPOmpo", c[0]) && (slot = bus_slot(c)) >= 0) {
        sw->row[toupper((unsigned char)c[0]) == 'M' ? ROW_MAIN : toupper((unsigned char)c[0]) == 'P' ? ROW_PRVW :
                ROW_LUMKEY].hilite = slot;
        o->act = RXA_CHANGED;
    } else if (is(c, "STAT")) {
        if (status(st, a1, o->text, sizeof(o->text))) o->rc = RC_ERROR;
    } else if (is(c, "FVID") || is(c, "NOMO")) {
        if ((!fs->frozen || is(c, "NOMO")) &&
            (inputs_grab(st->in, SLOT_DV1, st->dvbuf) || fs_freeze(fs, st->dvbuf))) o->rc = RC_ERROR;
        else if (is(c, "NOMO")) fs->stable[fs->cur] = 1;
        o->act = RXA_CHANGED;
    } else if (is(c, "LVID")) {
        fs_live(fs);
        o->act = RXA_CHANGED;
    } else if (is(c, "FMLD") || (is(c, "LRGB") && number(a2, &n) && n == 0)) {
        if (load_frame(st, a1)) o->rc = RC_ERROR;
        o->act = RXA_CHANGED;
    } else if (is(c, "FMSV") || (is(c, "SRGB") && argc > 3 && number(argv[3], &n) && n == 0)) {
        if (save_frame(st, a1)) o->rc = RC_ERROR;
    } else if (is(c, "BACK") && number(a1, &n) && n >= 0 && n <= 8) {
        sw->bgcolor = n + 1;
        o->act = RXA_CHANGED;
    } else if (is(c, "BORD") && number(a1, &n) && n >= 0 && n <= 7) {
        border = n;
    } else if (is(c, "TERM") && number(a1, &n) && n >= 0 && n <= 15) {
        terminators = n;
    } else if (is(c, "SGPI") && is(a1, "OFF")) {
        ;
    } else if (is(c, "FACE") && number(a1, &n) && n == 2) {
        ;
    } else if (is(c, "TOSW")) {
        o->act = RXA_TOSW;
    } else if (is(c, "TOWB")) {
        o->act = RXA_TOWB;
    } else if (is(c, "QUIT")) {
        o->act = RXA_QUIT;
    } else if (is(c, "MEMC") || is(c, "MEMF")) {
        snprintf(o->text, sizeof(o->text), "%lu", (unsigned long)AvailMem((is(c, "MEMC") ? MEMF_CHIP : MEMF_FAST) |
                 (toupper((unsigned char)a1[0]) == 'L' ? MEMF_LARGEST : 0)));
    } else if (is(c, "CKCG")) {         /* the CG is loaded while its port is there */
        strcpy(o->text, cg_loaded() ? "1" : "0");
    } else if (is(c, "LDCG") || is(c, "DPCG")) {
        o->act = is(c, "LDCG") ? RXA_LDCG : RXA_DPCG;
    } else if (is(c, "CKTP") || is(c, "CKLW") || is(c, "GROF")) {
        strcpy(o->text, "0");
    } else {
        o->rc = RC_ERROR;
    }
}
