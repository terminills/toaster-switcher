/* The Inputs and Outputs window. Above, pick the stream each switcher input shows - a camera, a capture device, a
 * file, what a plug-in brings; to the user they are all streams, named by what they are, or by the name given to
 * the input. Below, the outputs Program goes to through a plug-in, and the plug-ins that are loaded; Scan reads
 * their drawer again. A choice acts at once; Save keeps it in ENVARC: and ENV:, Use in ENV: only, Cancel puts
 * inputs and outputs back as they were. Where ENVARC: cannot be written to (a system started from a CD), Save is
 * not offered, as in the system's own preferences editors. The requesters of the Add buttons are asked by a thread of their own, so
 * the switcher goes on, and Program with it, while one is up.
 *
 * It is drawn in the Toaster's own look (skin.h) and lettered in NewTek's interface font; where the Toaster's
 * files do not have that font, in the screen's. */
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <graphics/rpattr.h>
#include <cybergraphx/cybergraphics.h>
#include <libraries/reqtools.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <proto/reqtools.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "inputswin.h"
#include "plugins.h"
#include "gfxfile.h"
#include "skin.h"

#define MAXENTRIES 64
#define PREFSDIR "VideoToaster"
#define PREFSFILE "VideoToaster/Inputs"

/* the picture holds native 0x00RRGGBB words */
#if AROS_BIG_ENDIAN
#define RGBFMT RECTFMT_0RGB32
#else
#define RGBFMT RECTFMT_BGR032
#endif

#define WIDTH 768               /* as wide as the Toaster's own panel */
#define MARGIN 10
#define GAP 16                  /* between the two columns */
#define BH SKIN_BUTTON_H
#define BW 112
#define SCROLLW 13
#define ROWS 10                 /* of the upper lists */
#define CAPH 7                  /* the interface font's capitals, at the top of its rows */
#define MAXCOLS 3
#define MAXTEXTS 96

static const char *const slotlabel[NSLOTS] = { "1", "2", "3", "4", "DV1", "DV2", "BKG" };
static const char *const slotkey[NSLOTS] = { "IN1", "IN2", "IN3", "IN4", "DV1", "DV2", "BKG" };

enum { L_INPUTS, L_SOURCES, L_OUTPUTS, L_PLUGINS, NLISTS };
enum { B_ADD, B_ADDFILE, B_SOUND, B_OUTSET, B_OUTONOFF, B_OUTCLEAR, B_SCAN, B_SAVE, B_USE, B_CANCEL, NBUTTONS };
#define MAXKINDS 5              /* kinds of a plug-in thing a requester offers as buttons */
static const char *const soundname[] = { "Follows video", "Always", "Off" };
static const char *const soundkey[] = { "follow", "always", "off" };

struct entry {
    char label[NAMELEN + 24];   /* its columns one after the other, each ended by a 0, and one more 0 */
    char spec[SPECLEN];
};

struct list {
    int x, y, w, rows;
    int tab[MAXCOLS];           /* where its columns start */
    struct entry *e;
    int n, top, sel;            /* sel: -1 for none */
};

struct button {
    int x, y, w;                /* w 0: not there */
    const char *label;
};

struct text {                   /* lettering in the screen's font: it goes on after the picture */
    const char *s;
    int x, y, maxx;
    uint32_t colour;
};

/* What an Add button asks, on its thread. The answer is a driver to add or an output's stream; the window takes it
 * up when the thread is through, and takes nothing else until then. */
struct dialogue {
    pthread_t thread;
    struct Task *owner, *task;
    ULONG sig;                  /* to owner: it is through */
    volatile int running, done, quit;
    struct Window *win;
    int what;                   /* the button it is for */
    const struct tsp_input *in[MAXKINDS];
    const struct tsp_output *out[MAXKINDS];
    int nk, full;               /* kinds to choose from; no output is free */
    char spec[SPECLEN];         /* the driver or the output's stream, "" for none */
};

struct inputswin {
    struct Window *win;
    const struct colorfont *font;
    struct RastPort *mrp;       /* measures the screen's font */
    int scale, h, fh, rh;       /* h: rows of the picture; fh: of a line of text; rh: of a row of a list */
    uint32_t *rgb, *sbuf;       /* the picture, and scaled */
    struct list list[NLISTS];
    struct button button[NBUTTONS];
    int labelx, namex, namey, namew, soundy, ruley;
    int press;                  /* the button held down, -1 for none */
    int cansave;                /* ENVARC: can be written to */
    int drag, grab;             /* the list whose scroller is held, -1 for none, and where on its knob */
    int focus, cur;             /* Name is typed into, at this character of edit */
    char edit[NICKLEN];
    int relabel, dirty;         /* a name changed; the picture is out of date */
    struct text text[MAXTEXTS];
    int ntext;
    struct inputs *in;
    struct inputprefs *prefs, saved;
    int slot;
    struct entry slots[NSLOTS], entries[MAXENTRIES];
    int out;                    /* the output selected */
    struct entry outs[MAXOUTPUTS], plugs[MAXPLUGINS + 1];
    int outstate[MAXOUTPUTS];   /* as the list shows them */
    struct dialogue dlg;
    int sigbit;
};

void inputprefs_default(struct inputprefs *p)
{
    memset(p, 0, sizeof(*p));
    strcpy(p->driver[0], "vhi:uvc");
    p->ndrivers = 1;
}

int inputprefs_load(struct inputprefs *p, const char *file)
{
    FILE *f = fopen(file, "r");
    char line[SPECLEN + 16];
    int s;
    if (!f) return -1;
    p->ndrivers = 0;
    memset(p->out, 0, sizeof(p->out));
    memset(p->outon, 0, sizeof(p->outon));
    while (fgets(line, sizeof(line), f)) {
        char *v = strchr(line, '='), *e;
        if (!v) continue;
        *v++ = 0;
        if ((e = strpbrk(v, "\r\n"))) *e = 0;
        if (!strcmp(line, "DRIVER") && p->ndrivers < MAXDRIVERS) {
            strncpy(p->driver[p->ndrivers++], v, SPECLEN - 1);
            continue;
        }
        if (!strncmp(line, "OUT", 3) && line[3] > '0' && line[3] <= '0' + MAXOUTPUTS) {
            if (!line[4]) strncpy(p->out[line[3] - '1'], v, SPECLEN - 1);
            else if (!strcmp(line + 4, "ON")) p->outon[line[3] - '1'] = atoi(v) == 1;
            continue;
        }
        for (s = 0; s < NSLOTS; s++) {
            size_t k = strlen(slotkey[s]);
            if (!strcmp(line, slotkey[s])) {
                strncpy(p->spec[s], v, SPECLEN - 1);
                p->has[s] = 1;
            } else if (!strncmp(line, slotkey[s], k) && !strcmp(line + k, "NAME")) {
                strncpy(p->nick[s], v, NICKLEN - 1);
            } else if (!strncmp(line, slotkey[s], k) && !strcmp(line + k, "SOUND")) {
                int m;
                for (m = 0; m < 3; m++)
                    if (!strcmp(v, soundkey[m])) p->sound[s] = m;
            }
        }
    }
    fclose(f);
    if (!p->ndrivers) {
        strcpy(p->driver[0], "vhi:uvc");
        p->ndrivers = 1;
    }
    return 0;
}

int inputprefs_save(const struct inputprefs *p, const char *file)
{
    FILE *f = fopen(file, "w");
    int i;
    if (!f) return -1;
    for (i = 0; i < p->ndrivers; i++)
        fprintf(f, "DRIVER=%s\n", p->driver[i]);
    for (i = 0; i < NSLOTS; i++)
        if (p->has[i])
            fprintf(f, "%s=%s\n", slotkey[i], p->spec[i]);
    for (i = 0; i < NSLOTS; i++)
        if (p->nick[i][0])
            fprintf(f, "%sNAME=%s\n", slotkey[i], p->nick[i]);
    for (i = 0; i < NSLOTS; i++)
        if (p->sound[i] > 0 && p->sound[i] < 3)
            fprintf(f, "%sSOUND=%s\n", slotkey[i], soundkey[p->sound[i]]);
    for (i = 0; i < MAXOUTPUTS; i++)
        if (p->out[i][0])
            fprintf(f, "OUT%d=%s\nOUT%dON=%d\n", i + 1, p->out[i], i + 1, p->outon[i]);
    return fclose(f) ? -1 : 0;
}

void inputprefs_start_outputs(const struct inputprefs *p)
{
    int n;
    for (n = 0; n < MAXOUTPUTS; n++)
        if (p->out[n][0] && p->outon[n]) out_start(n, p->out[n]);
}

const char *inputprefs_label(const struct inputprefs *p, struct inputs *in, int s)
{
    if (p->nick[s][0]) return p->nick[s];
    return inputs_slot_spec(in, s)[0] ? inputs_slot_name(in, s) : "";
}

static int save_to(const struct inputprefs *p, const char *dir)
{
    char path[64];
    BPTR l;
    snprintf(path, sizeof(path), "%s:%s", dir, PREFSDIR);
    if ((l = CreateDir((CONST_STRPTR)path))) UnLock(l);
    snprintf(path, sizeof(path), "%s:%s", dir, PREFSFILE);
    return inputprefs_save(p, path);
}

/* An entry's line; tabs part its columns. */
static void line(struct entry *e, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void line(struct entry *e, const char *fmt, ...)
{
    va_list ap;
    size_t i, n;
    va_start(ap, fmt);
    vsnprintf(e->label, sizeof(e->label) - 1, fmt, ap);
    va_end(ap);
    n = strlen(e->label);
    e->label[n + 1] = 0;
    for (i = 0; i < n; i++)
        if (e->label[i] == '\t') e->label[i] = 0;
}

/* "1  UGREEN Camera 2K", or with a nickname "1  Cam 1 (UGREEN Camera 2K)" */
static void build_inputs(struct inputswin *iw)
{
    int s;
    for (s = 0; s < NSLOTS; s++) {
        const char *n = inputs_slot_spec(iw->in, s)[0] ? inputs_slot_name(iw->in, s) : "None";
        if (iw->prefs->nick[s][0]) line(&iw->slots[s], "%s\t%s (%.40s)", slotlabel[s], iw->prefs->nick[s], n);
        else line(&iw->slots[s], "%s\t%s", slotlabel[s], n);
    }
    iw->list[L_INPUTS].n = NSLOTS;
}

/* "None", then every source of every driver, by name; the driver goes in brackets only with several drivers, and
 * where the name does not say it. */
static void build_sources(struct inputswin *iw)
{
    static char names[MAXSOURCES][NAMELEN];
    const struct tsp_input *kind;
    int d, i, n, ne = 0;
    line(&iw->entries[ne], "None");
    iw->entries[ne++].spec[0] = 0;
    for (d = 0; d < iw->prefs->ndrivers; d++) {
        char backend[16], *arg;
        const char *colon = strchr(iw->prefs->driver[d], ':');
        if (!colon || colon - iw->prefs->driver[d] >= (int)sizeof(backend)) continue;
        memcpy(backend, iw->prefs->driver[d], colon - iw->prefs->driver[d]);
        backend[colon - iw->prefs->driver[d]] = 0;
        arg = (char *)colon + 1;
        n = inputs_sources(iw->in, backend, arg, names, MAXSOURCES);
        for (i = 0; i < n && ne < MAXENTRIES; i++) {
            struct entry *e = &iw->entries[ne++];
            if (iw->prefs->ndrivers > 1 && strcmp(backend, "file") && !strstr(names[i], arg))
                line(e, "%.40s (%.20s)", names[i], arg);
            else line(e, "%.63s", names[i]);
            snprintf(e->spec, sizeof(e->spec), "%.15s:%.200s:%d", backend, arg, i);
        }
    }
    for (d = 0; (kind = plugins_input_kind(d)); d++) {         /* what the plug-ins bring without being asked */
        if (kind->ask) continue;
        n = inputs_sources(iw->in, kind->backend->name, "", names, MAXSOURCES);
        for (i = 0; i < n && ne < MAXENTRIES; i++) {
            struct entry *e = &iw->entries[ne++];
            line(e, "%.63s", names[i]);
            snprintf(e->spec, sizeof(e->spec), "%.15s::%d", kind->backend->name, i);
        }
    }
    iw->list[L_SOURCES].n = ne;
    inputs_release_plugins(iw->in);     /* listing them does not hold a plug-in */
}

/* The kind of output a spec ("net:8090") is of, NULL when no plug-in that is loaded has it. */
static const struct tsp_output *output_kind(const char *spec)
{
    const struct tsp_output *kind;
    const char *colon = strchr(spec, ':');
    int i;
    for (i = 0; colon && (kind = plugins_output_kind(i)); i++)
        if (strlen(kind->backend->name) == (size_t)(colon - spec) && !strncmp(kind->backend->name, spec, colon - spec))
            return kind;
    return NULL;
}

/* "1  on  Network stream (8090)" */
static void build_outputs(struct inputswin *iw)
{
    static const char *const statename[] = { "off", "starting", "on", "failed" };
    int n;
    for (n = 0; n < MAXOUTPUTS; n++) {
        const char *spec = iw->prefs->out[n], *arg = strchr(spec, ':');
        const struct tsp_output *kind = output_kind(spec);
        struct entry *e = &iw->outs[n];
        iw->outstate[n] = out_state(n);
        if (!spec[0]) line(e, "%d\tNone", n + 1);
        else if (!kind)
            line(e, "%d\t%s\t%.40s (no plug-in for it)", n + 1, iw->prefs->outon[n] ? "failed" : "off", spec);
        else if (arg[1])
            line(e, "%d\t%s\t%.30s (%.30s)", n + 1, statename[iw->outstate[n]], kind->label, arg + 1);
        else
            line(e, "%d\t%s\t%.60s", n + 1, statename[iw->outstate[n]], kind->label);
    }
    iw->list[L_OUTPUTS].n = MAXOUTPUTS;
}

/* "Colour bars 1.0 (bars.tsplugin), in use" */
static void build_plugins(struct inputswin *iw)
{
    struct plugin_info pi;
    int i;
    for (i = 0; i < MAXPLUGINS && !plugins_info(i, &pi); i++)
        line(&iw->plugs[i], "%.24s %d.%d (%.30s)%s", pi.name, pi.version, pi.revision, pi.file,
             pi.uses ? ", in use" : "");
    if (!i) line(&iw->plugs[i++], "None in %s", TSP_DRAWER);
    iw->list[L_PLUGINS].n = i;
}

static int current_entry(struct inputswin *iw)
{
    const char *spec = inputs_slot_spec(iw->in, iw->slot);
    int i;
    for (i = 0; i < iw->list[L_SOURCES].n; i++)
        if (!strcmp(iw->entries[i].spec, spec)) return i;
    return -1;
}

/* The lists show what is selected, and no further than they have lines. */
static void sync(struct inputswin *iw)
{
    int i;
    iw->list[L_INPUTS].sel = iw->slot;
    iw->list[L_SOURCES].sel = current_entry(iw);
    iw->list[L_OUTPUTS].sel = iw->out;
    iw->list[L_PLUGINS].sel = -1;
    for (i = 0; i < NLISTS; i++) {
        struct list *l = &iw->list[i];
        if (l->top > l->n - l->rows) l->top = l->n - l->rows;
        if (l->top < 0) l->top = 0;
    }
    iw->dirty = 1;
}

/* The line selected is among those a list shows. */
static void reveal(struct list *l)
{
    if (l->sel < 0) return;
    if (l->sel < l->top) l->top = l->sel;
    if (l->sel >= l->top + l->rows) l->top = l->sel - l->rows + 1;
}

/* The lower lists again: an output changed, or what is loaded did. */
static void refresh_lower(struct inputswin *iw)
{
    build_outputs(iw);
    build_plugins(iw);
    sync(iw);
}

static void refresh_lists(struct inputswin *iw)
{
    build_inputs(iw);
    sync(iw);
    reveal(&iw->list[L_SOURCES]);
}

static void reload_sources(struct inputswin *iw)
{
    build_sources(iw);
    refresh_lists(iw);
}

/* A driver ("vhi:uvc", "file:Work:Clips/show.mp4") joins the list, once. */
static void add_driver(struct inputswin *iw, const char *spec)
{
    int d;
    if (iw->prefs->ndrivers >= MAXDRIVERS || strlen(spec) >= SPECLEN) return;
    for (d = 0; d < iw->prefs->ndrivers && strcmp(iw->prefs->driver[d], spec); d++)
        ;
    if (d == iw->prefs->ndrivers) {
        strcpy(iw->prefs->driver[iw->prefs->ndrivers++], spec);
        reload_sources(iw);
    }
}

/* See a requester through that has come up (r is what bringing it up returned, *hi what it left). One the window
 * cannot wait for any longer goes as if it was cancelled. */
static IPTR attend(struct dialogue *d, struct Library *ReqToolsBase, IPTR r, struct rtHandlerInfo *const *hip)
{
    struct rtHandlerInfo *hi = *hip;
    if (r != CALL_HANDLER || !hi) return 0;
    while (r == CALL_HANDLER) {
        ULONG sigs = hi->DoNotWait ? 0 : Wait(hi->WaitMask | SIGBREAKF_CTRL_D);
        if (d->quit) r = rtReqHandler(hi, sigs, RTRH_EndRequest, REQ_CANCEL, TAG_DONE);
        else r = rtReqHandlerA(hi, sigs, NULL);
    }
    return r;
}

static int ask(struct dialogue *d, struct Library *ReqToolsBase, const char *fmt, const char *buttons, IPTR arg)
{
    struct rtHandlerInfo *hi = NULL;
    struct TagItem tags[] = { { RT_Window, (IPTR)d->win }, { RT_ReqHandler, (IPTR)&hi }, { TAG_DONE, 0 } };
    if (d->quit) return 0;
    return attend(d, ReqToolsBase, rtEZRequestA(fmt, buttons, NULL, &arg, tags), &hi);
}

/* Which of the n kinds, by their labels: 0 to n - 1, n for the choice first (when there is one), -1 for none. */
static int ask_kind(struct dialogue *d, struct Library *ReqToolsBase, const char *first, const char *const *labels,
                    int n)
{
    char buttons[(MAXKINDS + 2) * 34] = "";
    int i, r;
    if (!first && n == 1) return 0;
    if (first) snprintf(buttons, sizeof(buttons), "%.32s|", first);
    for (i = 0; i < n; i++)
        snprintf(buttons + strlen(buttons), sizeof(buttons) - strlen(buttons), "%.32s|", labels[i]);
    strcat(buttons, "Cancel");
    if (!(r = ask(d, ReqToolsBase, "%s", buttons, (IPTR)"Which kind?"))) return -1;
    if (!first) return r - 1;
    return r == 1 ? n : r - 2;
}

/* What is asked for (what, under the title label), into arg, which has what it starts with. */
static int ask_arg(struct dialogue *d, struct Library *ReqToolsBase, const char *label, const char *what, char *arg,
                   int size)
{
    struct rtHandlerInfo *hi = NULL;
    struct TagItem tags[] = {
        { RTGS_TextFmt, (IPTR)what }, { RT_Window, (IPTR)d->win }, { RT_ReqHandler, (IPTR)&hi }, { TAG_DONE, 0 }
    };
    if (d->quit) return 0;
    return attend(d, ReqToolsBase, rtGetStringA((UBYTE *)arg, size - 1, label, NULL, tags), &hi) != 0;
}

/* A plug-in's label goes on a button. */
static int label_ok(const char *label)
{
    return label && label[0] && !strpbrk(label, "|%");
}

#ifdef HAVE_FFMPEG
/* Add file: a video file as a stream. */
static void ask_file(struct dialogue *d, struct Library *ReqToolsBase)
{
    struct rtFileRequester *fr;
    struct rtHandlerInfo *hi = NULL;
    struct TagItem tags[] = { { RT_Window, (IPTR)d->win }, { RT_ReqHandler, (IPTR)&hi }, { TAG_DONE, 0 } };
    char file[108] = "";
    if (!(fr = rtAllocRequestA(RT_FILEREQ, NULL))) return;
    if (attend(d, ReqToolsBase, (IPTR)rtFileRequestA(fr, file, (char *)"Add a video file", tags), &hi) && file[0]) {
        strcpy(d->spec, "file:");
        strncat(d->spec, (const char *)fr->Dir, sizeof(d->spec) - 8);
        AddPart((STRPTR)d->spec + 5, (CONST_STRPTR)file, sizeof(d->spec) - 5);
    }
    rtFreeRequest(fr);
}
#endif

/* Add output: of a kind a plug-in brings, with what the kind asks for. */
static void ask_output(struct dialogue *d, struct Library *ReqToolsBase)
{
    const char *labels[MAXKINDS];
    char arg[SPECLEN - 20] = "";
    int i, k;
    for (i = 0; i < d->nk; i++) labels[i] = d->out[i]->label;
    if (d->full) {
        ask(d, ReqToolsBase, "All %ld outputs are taken.\nRemove one first.", "OK", MAXOUTPUTS);
    } else if (!d->nk) {
        ask(d, ReqToolsBase, "None of the plug-ins in %s\nbrings a kind of output.", "OK", (IPTR)TSP_DRAWER);
    } else if ((k = ask_kind(d, ReqToolsBase, NULL, labels, d->nk)) >= 0) {
        const struct tsp_output *kind = d->out[k];
        if (kind->def) snprintf(arg, sizeof(arg), "%s", kind->def);
        if (!kind->ask || ask_arg(d, ReqToolsBase, kind->label, kind->ask, arg, sizeof(arg)))
            snprintf(d->spec, sizeof(d->spec), "%s:%s", kind->backend->name, arg);
    }
}

/* Add streams: a video driver library (all of its devices), a video file, or a kind a plug-in brings that needs
 * to be told where its streams are. */
static void ask_stream(struct dialogue *d, struct Library *ReqToolsBase)
{
    const char *labels[MAXKINDS];
    char what[SPECLEN - 8] = "";
    size_t n;
    int i, k;
#ifdef HAVE_FFMPEG
    BPTR lock;
#endif
    for (i = 0; i < d->nk; i++) labels[i] = d->in[i]->label;
    if (d->nk && (k = ask_kind(d, ReqToolsBase, "Driver or file", labels, d->nk)) != d->nk) {
        if (k >= 0 && ask_arg(d, ReqToolsBase, d->in[k]->label, d->in[k]->ask, what, sizeof(what)))
            snprintf(d->spec, sizeof(d->spec), "%s:%s", d->in[k]->backend->name, what);
        return;
    }
    if (!ask_arg(d, ReqToolsBase, "Add stream",
                 "Video driver library (for example uvc for the USB cameras),\na video file, or a stream address",
                 what, sizeof(what)) || !what[0])
        return;
    n = strlen(what);
    if (n > 8 && !strcmp(what + n - 8, ".library") && !strchr(what, ':')) {
        snprintf(d->spec, sizeof(d->spec), "vhi:%s", what);
#ifdef HAVE_FFMPEG
    } else if ((lock = Lock((CONST_STRPTR)what, SHARED_LOCK))) {         /* a file */
        UnLock(lock);
        snprintf(d->spec, sizeof(d->spec), "file:%s", what);
#endif
    } else {
        ask(d, ReqToolsBase, "There is no plug-in for this kind of stream yet:\n%s", "OK", (IPTR)what);
    }
}

static void *dialogue(void *arg)
{
    struct dialogue *d = arg;
    struct Library *ReqToolsBase;
    d->task = FindTask(NULL);
    if ((ReqToolsBase = OpenLibrary((CONST_STRPTR)"reqtools.library", 38))) {
        if (d->what == B_ADD) ask_stream(d, ReqToolsBase);
        else if (d->what == B_OUTSET) ask_output(d, ReqToolsBase);
        else if (d->what == B_SAVE)
            ask(d, ReqToolsBase, "The settings could not be saved: %s cannot be written to.\n"
                "Use keeps them until the next start.", "OK", (IPTR)"ENVARC:");
#ifdef HAVE_FFMPEG
        else ask_file(d, ReqToolsBase);
#endif
        CloseLibrary(ReqToolsBase);
    }
    d->done = 1;
    Signal(d->owner, d->sig);
    return NULL;
}

/* An Add button was pressed: its requesters go up. The kinds they offer are the plug-ins' as they are now; Scan
 * waits like everything else, so they stay. */
static void dialogue_start(struct inputswin *iw, int what)
{
    struct dialogue *d = &iw->dlg;
    const struct tsp_input *in;
    const struct tsp_output *out;
    int i, n;
    if (d->running) return;
    memset(d, 0, sizeof(*d));
    d->owner = FindTask(NULL);
    d->sig = 1UL << iw->sigbit;
    d->win = iw->win;
    d->what = what;
    if (what == B_ADD) {
        for (i = 0; d->nk < MAXKINDS && (in = plugins_input_kind(i)); i++)
            if (in->ask && label_ok(in->label)) d->in[d->nk++] = in;
    } else if (what == B_OUTSET) {
        for (i = 0; d->nk < MAXKINDS && (out = plugins_output_kind(i)); i++)
            if (label_ok(out->label)) d->out[d->nk++] = out;
        for (n = 0; n < MAXOUTPUTS && iw->prefs->out[n][0]; n++)
            ;
        d->full = n == MAXOUTPUTS;
    }
    if (pthread_create(&d->thread, NULL, dialogue, d)) return;
    d->running = 1;
    SetWindowPointer(iw->win, WA_BusyPointer, TRUE, TAG_DONE);
}

/* The requesters are through: what they were answered with is taken up. A new output goes to the first one that
 * is free, and is off until it is switched on. */
static void dialogue_end(struct inputswin *iw)
{
    struct dialogue *d = &iw->dlg;
    int n;
    pthread_join(d->thread, NULL);
    d->running = 0;
    SetWindowPointer(iw->win, TAG_DONE);
    if (!d->spec[0]) return;
    if (d->what == B_OUTSET) {
        for (n = 0; n < MAXOUTPUTS && iw->prefs->out[n][0]; n++)
            ;
        if (n == MAXOUTPUTS) return;
        snprintf(iw->prefs->out[n], SPECLEN, "%s", d->spec);
        iw->prefs->outon[n] = 0;
        iw->out = n;
    } else {
        add_driver(iw, d->spec);
    }
    refresh_lower(iw);
}

/* Scan: the plug-ins' drawer is read again. One that is not in use is closed first, so a newer file takes its
 * place; one that an input shows or an output runs through stays as it is. */
static void scan(struct inputswin *iw)
{
    inputs_release_plugins(iw->in);
    plugins_scan();
    reload_sources(iw);
    refresh_lower(iw);
}

static int textw(struct inputswin *iw, const char *s)
{
    if (iw->font) return colorfont_width(iw->font, s);
    return (TextLength(iw->mrp, (CONST_STRPTR)s, strlen(s)) + iw->scale - 1) / iw->scale;
}

/* Lettering in what is h high at y, cut at maxx, on ground. s stays where it is until the picture is shown. */
static void text(struct inputswin *iw, int x, int y, int h, int maxx, const char *s, uint32_t colour, uint32_t ground)
{
    if (iw->font) {
        uint32_t pal[8];
        skin_ink(pal, colour, ground);
        colorfont_draw(iw->font, iw->rgb, WIDTH, maxx, iw->h, x, y + (h - CAPH) / 2, s, pal);
    } else if (iw->ntext < MAXTEXTS && maxx > x) {
        struct text *t = &iw->text[iw->ntext++];
        t->s = s;
        t->x = x;
        t->y = y + (h - iw->fh) / 2;
        t->maxx = maxx;
        t->colour = colour;
    }
}

/* A list's scroller: its track is th long from y + 2; the knob starts at *ky of it and is *kh long. */
static void knob(const struct list *l, int th, int *ky, int *kh)
{
    *kh = th * l->rows / l->n;
    if (*kh < 12) *kh = 12;
    *ky = (th - *kh) * l->top / (l->n - l->rows);
}

static void draw_list(struct inputswin *iw, const struct list *l)
{
    int th = l->rows * iw->rh, tw = l->w - 4 - (l->n > l->rows ? SCROLLW + 1 : 0), i, c;
    skin_well(iw->rgb, WIDTH, iw->h, l->x, l->y, l->w, th + 4);
    for (i = 0; i < l->rows && l->top + i < l->n; i++) {
        int y = l->y + 2 + i * iw->rh, sel = l->top + i == l->sel;
        const char *p = l->e[l->top + i].label;
        if (sel) skin_fill(iw->rgb, WIDTH, iw->h, l->x + 2, y, tw, iw->rh, SKIN_HILITE);
        for (c = 0; c < MAXCOLS && *p; c++, p += strlen(p) + 1)
            text(iw, l->x + 2 + l->tab[c], y, iw->rh, l->x + tw, p, sel ? 0x000000 : SKIN_TEXT,
                 sel ? SKIN_HILITE : SKIN_WELL);
    }
    if (l->n > l->rows) {
        int sx = l->x + l->w - 2 - SCROLLW, ky, kh;
        knob(l, th, &ky, &kh);
        skin_fill(iw->rgb, WIDTH, iw->h, sx - 1, l->y + 2, SCROLLW + 1, th, 0x383838);
        skin_button(iw->rgb, WIDTH, iw->h, sx, l->y + 2 + ky, SCROLLW, kh, 0);
    }
}

/* Whether Save has somewhere to save to: the test of the system's preferences editors (PrefsEditor.mcc). */
static int can_save(void)
{
    struct InfoData id;
    BPTR l = Lock((CONST_STRPTR)"ENVARC:", SHARED_LOCK);
    int ok;
    if (!l) return 0;
    ok = !Info(l, &id) || id.id_DiskState != ID_WRITE_PROTECTED;
    UnLock(l);
    return ok;
}

static void draw_button(struct inputswin *iw, int b, int down)
{
    const struct button *bt = &iw->button[b];
    const char *label = b == B_SOUND ? soundname[iw->prefs->sound[iw->slot]] : bt->label;
    int inner = bt->w - (b == B_SOUND ? 16 : 0), x;
    if (!bt->w) return;
    skin_button(iw->rgb, WIDTH, iw->h, bt->x, bt->y, bt->w, BH, (down ? SKIN_DOWN : 0) | (b == B_SOUND ? SKIN_ARROWS : 0));
    x = b == B_SOUND ? bt->x + 8 : bt->x + (inner - textw(iw, label)) / 2;
    text(iw, x + down, bt->y + down, BH, bt->x + inner - 2, label, b == B_SAVE && !iw->cansave ? SKIN_GHOST : 0x000000,
         down ? SKIN_FACE_DOWN : SKIN_FACE);
}

/* The width of the first n characters of s. */
static int prefixw(struct inputswin *iw, const char *s, int n)
{
    char t[NICKLEN];
    memcpy(t, s, n);
    t[n] = 0;
    return textw(iw, t);
}

static void draw(struct inputswin *iw)
{
    static const char *const head[NLISTS] = { "INPUTS", "STREAM", "OUTPUTS", "PLUG-INS" };
    int i, hh = iw->fh + 6;
    iw->ntext = 0;
    skin_fill(iw->rgb, WIDTH, iw->h, 0, 0, WIDTH, iw->h, SKIN_PANEL);
    for (i = 0; i < NLISTS; i++) {
        const struct list *l = &iw->list[i];
        int y = l->y - hh, tw = textw(iw, head[i]);
        skin_rule(iw->rgb, WIDTH, iw->h, l->x, y + hh / 2 - 1, 8);
        text(iw, l->x + 12, y, hh, l->x + l->w, head[i], 0x000000, SKIN_PANEL);
        skin_rule(iw->rgb, WIDTH, iw->h, l->x + 16 + tw, y + hh / 2 - 1, l->w - 16 - tw);
        draw_list(iw, l);
    }
    text(iw, iw->labelx, iw->namey, BH, iw->namex, "Name", SKIN_TEXT, SKIN_PANEL);
    skin_well(iw->rgb, WIDTH, iw->h, iw->namex, iw->namey, iw->namew, BH);
    text(iw, iw->namex + 6, iw->namey, BH, iw->namex + iw->namew - 4,
         iw->focus ? iw->edit : iw->prefs->nick[iw->slot], SKIN_TEXT, SKIN_WELL);
    if (iw->focus)
        skin_fill(iw->rgb, WIDTH, iw->h, iw->namex + 6 + prefixw(iw, iw->edit, iw->cur), iw->namey + 4, 2, BH - 7,
                  SKIN_HILITE);
    text(iw, iw->labelx, iw->soundy, BH, iw->namex, "Sound", SKIN_TEXT, SKIN_PANEL);
    for (i = 0; i < NBUTTONS; i++)
        draw_button(iw, i, i == iw->press);
    skin_rule(iw->rgb, WIDTH, iw->h, MARGIN, iw->ruley, WIDTH - 2 * MARGIN);
}

/* The picture into the window, and on it what is lettered in the screen's font. */
static void show(struct inputswin *iw)
{
    struct Window *w = iw->win;
    struct RastPort *rp = w->RPort;
    int s = iw->scale, x, y, k, i;
    if (s == 1) {
        WritePixelArray(iw->rgb, 0, 0, WIDTH * 4, rp, w->BorderLeft, w->BorderTop, WIDTH, iw->h, RGBFMT);
    } else {
        for (y = 0; y < iw->h; y++) {
            uint32_t *d = iw->sbuf + (size_t)y * s * WIDTH * s;
            for (x = 0; x < WIDTH; x++)
                for (k = 0; k < s; k++)
                    d[x * s + k] = iw->rgb[y * WIDTH + x];
            for (k = 1; k < s; k++)
                memcpy(d + (size_t)k * WIDTH * s, d, (size_t)WIDTH * s * 4);
        }
        WritePixelArray(iw->sbuf, 0, 0, WIDTH * s * 4, rp, w->BorderLeft, w->BorderTop, WIDTH * s, iw->h * s, RGBFMT);
    }
    SetDrMd(rp, JAM1);
    for (i = 0; i < iw->ntext; i++) {
        const struct text *t = &iw->text[i];
        struct TextExtent te;
        ULONG n = TextFit(rp, (CONST_STRPTR)t->s, strlen(t->s), &te, NULL, 1, (t->maxx - t->x) * s, 32767);
        SetRPAttrs(rp, RPTAG_FgColor, t->colour, TAG_DONE);
        Move(rp, w->BorderLeft + t->x * s, w->BorderTop + t->y * s + rp->TxBaseline);
        Text(rp, (CONST_STRPTR)t->s, n);
    }
}

static void redraw(struct inputswin *iw)
{
    draw(iw);
    show(iw);
    iw->dirty = 0;
}

/* Where everything is; the picture's height comes of it. */
static void layout(struct inputswin *iw)
{
    static const char *const names[NBUTTONS] = { "Add stream...", "Add file...", NULL, "Add output...", "On/Off",
                                                 "Remove", "Scan", "Save", "Use", "Cancel" };
    int cw = (WIDTH - 2 * MARGIN - GAP) / 2, x1 = MARGIN, x2 = MARGIN + cw + GAP, hh = iw->fh + 6, y = 6, i;
    int w3 = (cw - 16) / 3, lw = textw(iw, "Sound") + 8;
    struct list *l = iw->list;
    struct button *b = iw->button;
    for (i = 0; i < NBUTTONS; i++) {
        b[i].label = names[i];
        b[i].w = BW;
    }
    for (i = 0; i < NLISTS; i++) {
        l[i].x = i & 1 ? x2 : x1;
        l[i].w = cw;
        l[i].rows = i < L_OUTPUTS ? ROWS : MAXOUTPUTS;
        l[i].tab[0] = 4;
    }
    l[L_INPUTS].e = iw->slots;
    l[L_SOURCES].e = iw->entries;
    l[L_OUTPUTS].e = iw->outs;
    l[L_PLUGINS].e = iw->plugs;
    l[L_INPUTS].tab[1] = 4 + textw(iw, "BKG") + 10;
    l[L_OUTPUTS].tab[1] = 4 + textw(iw, "4") + 10;
    l[L_OUTPUTS].tab[2] = l[L_OUTPUTS].tab[1] + textw(iw, "starting") + 10;

    y += hh;
    l[L_INPUTS].y = l[L_SOURCES].y = y;
    y += ROWS * iw->rh + 4 + 8;
    iw->labelx = x1;
    iw->namex = x1 + lw;
    iw->namey = y;
    iw->namew = cw - lw;
    b[B_ADD].x = x2;
    b[B_ADDFILE].x = x2 + BW + 8;
    b[B_ADD].y = b[B_ADDFILE].y = y;
#ifndef HAVE_FFMPEG
    b[B_ADDFILE].w = 0;
#endif
    y += BH + 6;
    iw->soundy = y;
    b[B_SOUND].x = x1 + lw;
    b[B_SOUND].y = y;
    b[B_SOUND].w = textw(iw, soundname[0]) + 36;
    y += BH + 10 + hh;
    l[L_OUTPUTS].y = l[L_PLUGINS].y = y;
    y += MAXOUTPUTS * iw->rh + 4 + 8;
    for (i = 0; i < 3; i++) {
        b[B_OUTSET + i].x = x1 + i * (w3 + 8);
        b[B_OUTSET + i].y = y;
        b[B_OUTSET + i].w = w3;
    }
    b[B_SCAN].x = x2;
    b[B_SCAN].y = y;
    y += BH + 10;
    iw->ruley = y;
    y += 3 + 8;
    b[B_SAVE].x = x1;
    b[B_USE].x = (WIDTH - BW) / 2;
    b[B_CANCEL].x = WIDTH - MARGIN - BW;
    b[B_SAVE].y = b[B_USE].y = b[B_CANCEL].y = y;
    iw->h = y + BH + 8;
}

/* Name: what was typed is the input's name from now on. */
static void nick_commit(struct inputswin *iw)
{
    const char *t = iw->edit;
    if (!iw->focus) return;
    iw->focus = 0;
    while (*t == ' ') t++;
    snprintf(iw->prefs->nick[iw->slot], NICKLEN, "%s", t);
    iw->relabel = 1;
    refresh_lists(iw);
}

struct inputswin *inputswin_open(struct inputswin *iw, struct Screen *scr, struct inputs *in, struct inputprefs *p,
                                 int slot, const struct colorfont *font, int scale)
{
    int s, ww, wh;

    if (iw) {
        if (slot >= 0 && slot != iw->slot) {
            nick_commit(iw);
            iw->slot = slot;
        }
        iw->cansave = can_save();
        refresh_lists(iw);
        redraw(iw);
        WindowToFront(iw->win);
        ActivateWindow(iw->win);
        return iw;
    }
    if (!(iw = AllocVec(sizeof(*iw), MEMF_ANY | MEMF_CLEAR))) return NULL;
    if ((iw->sigbit = AllocSignal(-1)) < 0) {
        FreeVec(iw);
        return NULL;
    }
    iw->in = in;
    iw->prefs = p;
    iw->cansave = can_save();
    iw->saved = *p;
    for (s = 0; s < NSLOTS; s++) {             /* what Cancel goes back to */
        strncpy(iw->saved.spec[s], inputs_slot_spec(in, s), SPECLEN - 1);
        iw->saved.has[s] = p->has[s];
    }
    iw->slot = slot;
    iw->press = iw->drag = -1;
    iw->font = font;
    iw->mrp = &scr->RastPort;
    iw->scale = scale < 1 ? 1 : scale;
    iw->fh = font ? font->ysize : (scr->RastPort.TxHeight + iw->scale - 1) / iw->scale;
    iw->rh = iw->fh + 3;
    layout(iw);
    if (!(iw->rgb = malloc((size_t)WIDTH * iw->h * 4))) goto fail;
    if (iw->scale > 1 && !(iw->sbuf = malloc((size_t)WIDTH * iw->scale * iw->h * iw->scale * 4))) goto fail;
    build_inputs(iw);
    build_sources(iw);
    build_outputs(iw);
    build_plugins(iw);
    sync(iw);
    reveal(&iw->list[L_SOURCES]);

    ww = WIDTH * iw->scale + scr->WBorLeft + scr->WBorRight;
    wh = iw->h * iw->scale + scr->WBorTop + scr->Font->ta_YSize + 1 + scr->WBorBottom;
    iw->win = OpenWindowTags(NULL, WA_Title, (IPTR)"Video Toaster Inputs and Outputs", WA_InnerWidth,
                             WIDTH * iw->scale, WA_InnerHeight, iw->h * iw->scale,
                             WA_Left, ww < scr->Width ? (scr->Width - ww) / 2 : 0,
                             WA_Top, wh < scr->Height ? (scr->Height - wh) / 2 : 0,
                             WA_PubScreen, (IPTR)scr, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                             WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_ReportMouse, TRUE,
                             WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE |
                                 IDCMP_RAWKEY | IDCMP_VANILLAKEY | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW,
                             TAG_DONE);
    if (!iw->win) goto fail;
    SetFont(iw->win->RPort, scr->RastPort.Font);
    redraw(iw);
    return iw;
fail:
    FreeSignal(iw->sigbit);
    free(iw->sbuf);
    free(iw->rgb);
    FreeVec(iw);
    return NULL;
}

ULONG inputswin_sigmask(struct inputswin *iw)
{
    return iw ? 1UL << iw->win->UserPort->mp_SigBit | 1UL << iw->sigbit : 0;
}

void inputswin_close(struct inputswin *iw)
{
    if (!iw) return;
    while (iw->dlg.running && !iw->dlg.done) {          /* a requester is up: it goes first */
        iw->dlg.quit = 1;
        if (iw->dlg.task) Signal(iw->dlg.task, SIGBREAKF_CTRL_D);
        Delay(1);
    }
    if (iw->dlg.running) pthread_join(iw->dlg.thread, NULL);
    CloseWindow(iw->win);
    SetSignal(0, 1UL << iw->sigbit);
    FreeSignal(iw->sigbit);
    free(iw->sbuf);
    free(iw->rgb);
    FreeVec(iw);
}

void inputswin_poll(struct inputswin *iw)
{
    int n;
    for (n = 0; n < MAXOUTPUTS && out_state(n) == iw->outstate[n]; n++)
        ;
    if (n < MAXOUTPUTS) refresh_lower(iw);
    if (iw->dirty) redraw(iw);
}

static int inside(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static int button_at(struct inputswin *iw, int x, int y)
{
    int b;
    for (b = 0; b < NBUTTONS; b++)
        if (inside(x, y, iw->button[b].x, iw->button[b].y, iw->button[b].w, BH))
            return b == B_SAVE && !iw->cansave ? -1 : b;
    return -1;
}

static int list_at(struct inputswin *iw, int x, int y)
{
    int i;
    for (i = 0; i < NLISTS; i++)
        if (inside(x, y, iw->list[i].x, iw->list[i].y + 2, iw->list[i].w, iw->list[i].rows * iw->rh)) return i;
    return -1;
}

static void scroll(struct list *l, int by)
{
    l->top += by;
    if (l->top > l->n - l->rows) l->top = l->n - l->rows;
    if (l->top < 0) l->top = 0;
}

/* A line of a list was clicked. */
static void pick(struct inputswin *iw, int list, int row, int *changed)
{
    switch (list) {
    case L_INPUTS:
        iw->slot = row;
        refresh_lists(iw);
        break;
    case L_SOURCES:
        inputs_set(iw->in, iw->slot, iw->entries[row].spec);
        strcpy(iw->prefs->spec[iw->slot], iw->entries[row].spec);
        iw->prefs->has[iw->slot] = 1;
        *changed = 1;
        inputs_release_plugins(iw->in);         /* what it showed before */
        build_inputs(iw);
        refresh_lower(iw);
        break;
    case L_OUTPUTS:
        iw->out = row;
        sync(iw);
        break;
    }
}

/* A key typed into Name. */
static void key(struct inputswin *iw, int c)
{
    int n = strlen(iw->edit);
    if (c == 13 || c == 9) {
        nick_commit(iw);
    } else if (c == 27) {
        iw->focus = 0;
    } else if (c == 8) {
        if (iw->cur > 0) {
            memmove(iw->edit + iw->cur - 1, iw->edit + iw->cur, n - iw->cur + 1);
            iw->cur--;
        }
    } else if (c == 127) {
        if (iw->cur < n) memmove(iw->edit + iw->cur, iw->edit + iw->cur + 1, n - iw->cur);
    } else if (((c >= 32 && c < 127) || c >= 160) && n < NICKLEN - 1) {
        memmove(iw->edit + iw->cur + 1, iw->edit + iw->cur, n - iw->cur + 1);
        iw->edit[iw->cur++] = c;
    }
    iw->dirty = 1;
}

static void down(struct inputswin *iw, int x, int y, int *changed)
{
    int i, b;
    if (inside(x, y, iw->namex, iw->namey, iw->namew, BH)) {
        int n;
        if (!iw->focus) {
            strcpy(iw->edit, iw->prefs->nick[iw->slot]);
            iw->focus = 1;
        }
        n = strlen(iw->edit);
        for (iw->cur = 0; iw->cur < n; iw->cur++)       /* the cursor goes between the letters nearest */
            if (iw->namex + 6 + (prefixw(iw, iw->edit, iw->cur) + prefixw(iw, iw->edit, iw->cur + 1)) / 2 > x) break;
        iw->dirty = 1;
        return;
    }
    nick_commit(iw);
    if ((b = button_at(iw, x, y)) >= 0) {
        iw->press = b;
        iw->dirty = 1;
    } else if ((i = list_at(iw, x, y)) >= 0) {
        struct list *l = &iw->list[i];
        int ry = y - l->y - 2, row = l->top + ry / iw->rh;
        if (l->n > l->rows && x >= l->x + l->w - 3 - SCROLLW) {
            int ky, kh;
            knob(l, l->rows * iw->rh, &ky, &kh);
            if (ry < ky) scroll(l, 1 - l->rows);
            else if (ry >= ky + kh) scroll(l, l->rows - 1);
            else {
                iw->drag = i;
                iw->grab = ry - ky;
            }
            iw->dirty = 1;
        } else if (row < l->n && i != L_PLUGINS) {
            pick(iw, i, row, changed);
        }
    }
}

static void move(struct inputswin *iw, int x, int y)
{
    if (iw->drag >= 0) {
        struct list *l = &iw->list[iw->drag];
        int th = l->rows * iw->rh, ky, kh, top;
        knob(l, th, &ky, &kh);
        top = th > kh ? ((y - l->y - 2 - iw->grab) * (l->n - l->rows) + (th - kh) / 2) / (th - kh) : 0;
        if (top > l->n - l->rows) top = l->n - l->rows;
        if (top < 0) top = 0;
        if (top != l->top) {
            l->top = top;
            iw->dirty = 1;
        }
    } else if (iw->press >= 0 && button_at(iw, x, y) != iw->press) {       /* off the button: it is let go */
        iw->press = -1;
        iw->dirty = 1;
    }
}

enum { STAY, CLOSE, CANCEL };

/* A button was pressed. */
static int act(struct inputswin *iw, int b)
{
    int n = iw->out;
    switch (b) {
    case B_ADD:
#ifdef HAVE_FFMPEG
    case B_ADDFILE:
#endif
    case B_OUTSET:
        dialogue_start(iw, b);
        break;
    case B_SOUND:
        iw->prefs->sound[iw->slot] = (iw->prefs->sound[iw->slot] + 1) % 3;
        inputs_sound(iw->in, iw->slot, iw->prefs->sound[iw->slot]);
        iw->dirty = 1;
        break;
    case B_OUTONOFF:
    case B_OUTCLEAR:
        if (!iw->prefs->out[n][0]) break;
        if (b == B_OUTONOFF && !iw->prefs->outon[n]) {
            iw->prefs->outon[n] = 1;
            out_start(n, iw->prefs->out[n]);
        } else {
            out_stop(n);
            iw->prefs->outon[n] = 0;
            if (b == B_OUTCLEAR) iw->prefs->out[n][0] = 0;
        }
        refresh_lower(iw);
        break;
    case B_SCAN:
        scan(iw);
        break;
    case B_SAVE:
        if (save_to(iw->prefs, "ENVARC")) {     /* it could not be written after all: said, and the window stays */
            dialogue_start(iw, b);
            break;
        }
        /* fall through */
    case B_USE:
        save_to(iw->prefs, "ENV");
        return CLOSE;
    case B_CANCEL:
        return CANCEL;
    }
    return STAY;
}

int inputswin_handle(struct inputswin *iw, int *changed)
{
    struct IntuiMessage *m;
    int s, n, r = STAY;
    if (iw->dlg.running && iw->dlg.done) dialogue_end(iw);
    while (r == STAY && (m = (struct IntuiMessage *)GetMsg(iw->win->UserPort))) {
        ULONG cl = m->Class;
        UWORD code = m->Code;
        int x = (m->MouseX - iw->win->BorderLeft) / iw->scale, y = (m->MouseY - iw->win->BorderTop) / iw->scale;
        ReplyMsg(&m->ExecMessage);
        if (iw->dlg.running && cl != IDCMP_REFRESHWINDOW) continue;     /* a requester is up */
        switch (cl) {
        case IDCMP_REFRESHWINDOW:
            BeginRefresh(iw->win);
            show(iw);
            EndRefresh(iw->win, TRUE);
            break;
        case IDCMP_CLOSEWINDOW:
            r = CANCEL;
            break;
        case IDCMP_INACTIVEWINDOW:
            iw->drag = -1;
            if (iw->press >= 0) iw->dirty = 1;
            iw->press = -1;
            break;
        case IDCMP_VANILLAKEY:
            if (iw->focus) key(iw, code);
            else if (code == 27) r = CANCEL;    /* Esc */
            break;
        case IDCMP_RAWKEY:
            if (iw->focus && code == 0x4f && iw->cur > 0) {                         /* cursor left */
                iw->cur--;
                iw->dirty = 1;
            } else if (iw->focus && code == 0x4e && iw->edit[iw->cur]) {            /* right */
                iw->cur++;
                iw->dirty = 1;
            } else if ((code == 0x7a || code == 0x7b) && (n = list_at(iw, x, y)) >= 0) {   /* the wheel */
                scroll(&iw->list[n], code == 0x7a ? -1 : 1);
                iw->dirty = 1;
            }
            break;
        case IDCMP_MOUSEMOVE:
            move(iw, x, y);
            break;
        case IDCMP_MOUSEBUTTONS:
            if (code == SELECTDOWN) {
                down(iw, x, y, changed);
            } else if (code == SELECTUP) {
                int b = iw->press;
                iw->drag = iw->press = -1;
                if (b >= 0) {
                    iw->dirty = 1;
                    r = act(iw, b);
                }
            }
            break;
        }
    }
    if (iw->relabel) *changed = 1;
    iw->relabel = 0;
    if (r == CLOSE) {
        inputswin_close(iw);
        return 0;
    }
    if (r == STAY) {
        if (iw->dirty) redraw(iw);
        return 1;
    }
    for (s = 0; s < NSLOTS; s++)
        if (strcmp(inputs_slot_spec(iw->in, s), iw->saved.spec[s])) {
            inputs_set(iw->in, s, iw->saved.spec[s]);
            *changed = 1;
        }
    for (s = 0; s < NSLOTS; s++) {
        if (strcmp(iw->prefs->nick[s], iw->saved.nick[s])) *changed = 1;
        if (iw->prefs->sound[s] != iw->saved.sound[s]) inputs_sound(iw->in, s, iw->saved.sound[s]);
    }
    inputs_release_plugins(iw->in);
    for (n = 0; n < MAXOUTPUTS; n++)
        if (strcmp(iw->prefs->out[n], iw->saved.out[n]) || iw->prefs->outon[n] != iw->saved.outon[n]) {
            out_stop(n);
            if (iw->saved.out[n][0] && iw->saved.outon[n]) out_start(n, iw->saved.out[n]);
        }
    *iw->prefs = iw->saved;
    inputswin_close(iw);
    return 0;
}
