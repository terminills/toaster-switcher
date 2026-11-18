/* The Editor's ARexx functions (see rexxedit.h). */
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <rexx/errors.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rexxedit.h"
#include "rexxreq.h"
#include "swproject.h"
#include "croutontags.h"
#include "effect.h"
#include "record.h"

#define ERR_FNRETURN ERR10_012
#define ERR_BADARGNUM ERR10_017
#define ERR_BADARG ERR10_018
#define FIELDS 60               /* the Toaster counts NTSC fields */
#define TICKS 50

#define NAMELEN 28
#define CMDLEN 127
#define MAXENTRIES 25

/* Tools and Programs: the Toaster's own come first, and scripts count on that (the first tool added is 12). */
static struct rxlist {
    int nsys, n;
    struct {
        char name[NAMELEN + 1], cmd[CMDLEN + 1];
        int flags;              /* 1: an ARexx program */
    } e[MAXENTRIES];
} lists[2] = { { 12, 12, { { "Add croutons", "", 0 } } }, { 6, 6, { { "", "", 0 } } } };

static int viewmode = 4;
static int filereq;             /* 0 none asked for, 1 asked for or open, 2 cancelled, 3 a file */
static char freqtitle[128], freqdir[256], freqfile[108], freqpath[512];

static int same(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return 0;
    return !*a && !*b;
}

static void fail(struct rxeout *o, long rc2)
{
    o->rc = RC_ERROR;
    o->rc2 = rc2;
}

static void say(struct rxeout *o, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(o->text, sizeof(o->text), fmt, ap);
    va_end(ap);
    o->result = 1;
}

static void deselect(struct rxedit *st)
{
    struct switcher *sw = st->sw;
    if (sw->fxsel >= 0 && sw->fxsel != *st->ovl) effect_free_frames(&sw->fx[sw->fxsel]);
    sw->fxsel = -1;
}

static void pick(struct rxedit *st, struct rxeout *o, int i)
{
    select_effect(st->sw, i, *st->ovl);
    show_selected(st->sw);
    o->act = RXE_CHANGED;
}

/* The place Row,Column names: rows and columns count from 1, and scripts take column 0 for the one before. */
static int place(const char *const *argv)
{
    return (atol(argv[0]) - 1) * GRID_COLS + atol(argv[1]) - 1;
}

static struct effect *crouton(struct rxedit *st, long i)
{
    return i >= 0 && i < st->sw->nfx ? &st->sw->fx[i] : NULL;
}

/* How long a sequence stays on the crouton, in 1/50 s. */
static long duration(const struct rxedit *st, const struct effect *e)
{
    long hold = e->ctl_hold >= 0 ? e->ctl_hold : st->hold;
    uint32_t delay = 0;
    if (e->type == FX_REXX && e->ctl_hold < 0)
        return effect_tag(e, TAG_VALUE(TAGN_Delay), &delay, NULL) ? (long)delay * TICKS / FIELDS : 0;
    if (e->type == FX_CTRL || e->type == FX_REXX || effect_main_slot(e) >= 0) return hold;
    return effect_ticks(e, st->sw->speed, st->speedticks[st->sw->speed]) + hold;       /* it holds after its transition */
}

/* CROUTONTYPE and the four tag functions (what: G S C V) for the crouton e, with the tag's name and value. */
static void tagcall(struct rxeout *o, int what, struct effect *e, const char *name, const char *value)
{
    uint32_t v = 0;
    const uint8_t *data = NULL;
    int n, table = 0;
    if (!e || (what == 'T' && e->entrylen < 8)) {
        fail(o, ERR_FNRETURN);
        return;
    }
    if (what == 'T') {
        say(o, "%.4s", (const char *)e->entry + 4);
        return;
    }
    if (!(n = crouton_tag(name, &table))) {
        fail(o, ERR_BADARG);
        return;
    }
    if (what == 'C' || what == 'V') {
        say(o, "1");
        return;
    }
    if (!table) say(o, "%ld", effect_tag(e, TAG_VALUE(n), &v, NULL) ? (long)(int32_t)v : 0L);
    else if (effect_tag(e, TAG_TABLE(n), &v, &data))
        say(o, "%.*s", v < sizeof(o->text) ? (int)v : (int)sizeof(o->text) - 1, (const char *)data);
    else say(o, "");
    if (what == 'S') {          /* the answer is what it was before */
        if (table ? effect_set_tag(e, TAG_TABLE(n), strlen(value) + 1, value) :
                    effect_set_tag(e, TAG_VALUE(n), (uint32_t)atol(value), NULL)) {
            o->result = 0;
            fail(o, ERR_FNRETURN);
        }
        o->act = RXE_CHANGED;
    }
}

/* ADDTOOL, REMTOOL, TOOLNAME, TOOLCMD, TOOLNUM and the same for programs (what: A R N C I). */
static void listcall(struct rxlist *l, struct rxeout *o, int what, int argc, const char *const *argv)
{
    long i;
    if (argc < (what == 'A' ? 2 : 1)) {
        fail(o, ERR_BADARGNUM);
        return;
    }
    i = atol(argv[0]);
    if (what == 'A') {
        if (l->n >= MAXENTRIES) {
            fail(o, ERR_FNRETURN);
            return;
        }
        snprintf(l->e[l->n].name, sizeof(l->e[0].name), "%s", argv[0]);
        snprintf(l->e[l->n].cmd, sizeof(l->e[0].cmd), "%s", argv[1]);
        l->e[l->n].flags = argc > 2 ? atoi(argv[2]) : 0;
        say(o, "%d", l->n++);
    } else if (what == 'I') {
        for (i = 0; i < l->n && strcmp(argv[0], l->e[i].name); i++)
            ;
        if (i < l->n) say(o, "%ld", i);
        else fail(o, ERR_BADARG);
    } else if (i < (what == 'R' ? l->nsys : 0) || i >= l->n) {
        fail(o, ERR_BADARG);
    } else if (what == 'R') {
        memmove(&l->e[i], &l->e[i + 1], (l->n - i - 1) * sizeof(l->e[0]));
        l->n--;
    } else {
        say(o, "%s", what == 'N' ? l->e[i].name : l->e[i].cmd);
    }
}

int rxedit_list(int list, const char **names, int max)
{
    const struct rxlist *l = &lists[list];
    int i;
    for (i = 0; i < l->n - l->nsys && i < max; i++) names[i] = l->e[l->nsys + i].name;
    return i;
}

void rxedit_start(struct rxedit *st, int list, int i)
{
    const struct rxlist *l = &lists[list];
    BPTR in, out;
    if (i < 0 || i >= l->n - l->nsys) return;
    i += l->nsys;
    if (l->e[i].flags & 1) {
        rexx_run(st->host, l->e[i].cmd);
        return;
    }
    in = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);          /* on its own: the switcher goes on */
    out = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
    if (!in || !out || SystemTags((CONST_STRPTR)l->e[i].cmd, SYS_Asynch, TRUE, SYS_Input, (IPTR)in, SYS_Output, (IPTR)out,
                                  TAG_DONE) == -1) {
        if (in) Close(in);
        if (out) Close(out);
    }
}

void rxedit_filereq(struct rxedit *st)
{
    if (filereq != 1) return;
    filereq = req_file(st->w, freqtitle, freqdir, freqfile, freqpath, sizeof(freqpath)) ? 3 : 2;
}

void rxeditor(struct rxedit *st, const char *fn, int argc, const char *const *argv, struct rxeout *o)
{
    static const char *const flyer[] = { "CROUTONRUN", "REQ_FLYERJOG", "TBC", "FLYEROUT", "MAKEICON" };
    static const struct { const char *name; int what, crnum, args; } tagfn[] = {
        { "CROUTONTYPE", 'T', 0, 0 }, { "CRNUMTYPE", 'T', 1, 1 },
        { "CROUTONGETTAG", 'G', 0, 1 }, { "CRNUMGETTAG", 'G', 1, 2 },
        { "CROUTONSETTAG", 'S', 0, 2 }, { "CRNUMSETTAG", 'S', 1, 3 },
        { "CROUTONCHECKTAG", 'C', 0, 1 }, { "CRNUMCHECKTAG", 'C', 1, 2 },
        { "CROUTONSAVETAG", 'V', 0, 1 }, { "CRNUMSAVETAG", 'V', 1, 2 } };
    static const struct { const char *name; int list, what; } listfn[] = {
        { "ADDTOOL", RXL_TOOLS, 'A' }, { "REMTOOL", RXL_TOOLS, 'R' }, { "TOOLNAME", RXL_TOOLS, 'N' },
        { "TOOLCMD", RXL_TOOLS, 'C' }, { "TOOLNUM", RXL_TOOLS, 'I' },
        { "ADDPROGRAM", RXL_PROGRAMS, 'A' }, { "REMPROGRAM", RXL_PROGRAMS, 'R' }, { "PROGRAMNAME", RXL_PROGRAMS, 'N' },
        { "PROGRAMCMD", RXL_PROGRAMS, 'C' }, { "PROGRAMNUM", RXL_PROGRAMS, 'I' } };
    struct switcher *sw = st->sw;
    struct effect *cur = FXCUR(sw);
    unsigned i;

    memset(o, 0, sizeof(*o));
    for (i = 0; i < sizeof(flyer) / sizeof(flyer[0]); i++)
        if (same(fn, flyer[i])) {
            fail(o, ERR_FNRETURN);
            return;
        }
    for (i = 0; i < sizeof(tagfn) / sizeof(tagfn[0]); i++)
        if (same(fn, tagfn[i].name)) {
            int c = tagfn[i].crnum;
            if (argc < tagfn[i].args) {
                fail(o, ERR_BADARGNUM);
                return;
            }
            if (tagfn[i].what == 'T' && !c && argc == 2) {
                pick(st, o, place(argv));
                cur = FXCUR(sw);
            }
            tagcall(o, tagfn[i].what, c ? crouton(st, atol(argv[0])) : cur, argc > c ? argv[c] : "",
                    argc > c + 1 ? argv[c + 1] : "");
            return;
        }
    for (i = 0; i < sizeof(listfn) / sizeof(listfn[0]); i++)
        if (same(fn, listfn[i].name)) {
            listcall(&lists[listfn[i].list], o, listfn[i].what, argc, argv);
            return;
        }

    if (same(fn, "REQ_TELL")) {
        if (argc < 1) fail(o, ERR_BADARGNUM);
        else say(o, "%d", req_tell(st->w, argv, argc > 4 ? 4 : argc));
    } else if (same(fn, "REQ_BUTTONS")) {
        char states[5] = "0000";
        int n = argc - 2 > 4 ? 4 : argc - 2, k;
        if (argc < 3) {
            fail(o, ERR_BADARGNUM);
            return;
        }
        for (k = 0; k < n && argv[1][k]; k++) states[k] = argv[1][k] == '1' ? '1' : '0';
        states[n] = 0;
        say(o, "%s", req_buttons(st->w, argv[0], states, argv + 2, n) ? states : "CANCEL");
    } else if (same(fn, "REQ_OPEN")) {
        if (argc < 1) fail(o, ERR_BADARGNUM);
        else req_notice(st->w, argv, argc > 4 ? 4 : argc);
    } else if (same(fn, "REQ_CLOSE")) {
        req_notice_close();
    } else if (same(fn, "REQ_ERROR")) {
        req_message(st->w, argc ? argv[0] : NULL);
    } else if (same(fn, "REQ_TIME")) {
        char tc[12] = "00:00:00:00";
        if (argc < 1) {
            fail(o, ERR_BADARGNUM);
            return;
        }
        if (argc > 1) snprintf(tc, sizeof(tc), "%s", argv[1]);
        say(o, "%s", req_time(st->w, argv[0], tc) ? tc : "CANCEL");
    } else if (same(fn, "REQ_NUMBER")) {
        long v = argc > 1 ? atol(argv[1]) : 0;
        if (argc < 1) fail(o, ERR_BADARGNUM);
        else if (req_number(st->w, argv[0], &v, argc > 2 ? atol(argv[2]) : 0, argc > 3 ? atol(argv[3]) : 0xFFFFFF))
            say(o, "%ld", v);
        else say(o, "CANCEL");
    } else if (same(fn, "REQ_STRING")) {
        char s[81] = "";
        if (argc < 1) {
            fail(o, ERR_BADARGNUM);
            return;
        }
        if (argc > 1) snprintf(s, sizeof(s), "%s", argv[1]);
        say(o, "%s", req_string(st->w, argv[0], s, sizeof(s)) ? s : "CANCEL");
    } else if (same(fn, "STARTFILEREQ")) {
        snprintf(freqtitle, sizeof(freqtitle), "%s", argc > 0 ? argv[0] : "");
        snprintf(freqdir, sizeof(freqdir), "%s", argc > 1 ? argv[1] : "");
        snprintf(freqfile, sizeof(freqfile), "%s", argc > 2 ? argv[2] : "");
        filereq = 1;
        say(o, "");
        o->act = RXE_FILEREQ;
    } else if (same(fn, "QUERYFILEREQ")) {
        say(o, "%s", filereq == 2 ? "0" : filereq == 3 ? freqpath : "");
    } else if (same(fn, "PROJECTLOAD") || same(fn, "PROJECTSAVE")) {
        if (argc < 1) {
            fail(o, ERR_BADARGNUM);
        } else if (same(fn, "PROJECTSAVE")) {
            if (save_project_to(sw, argv[0])) fail(o, ERR_FNRETURN);
        } else if (load_project(sw, argv[0])) {
            fail(o, ERR_FNRETURN);
        } else {
            *st->ovl = -1;
            select_effect(sw, 0, -1);
            o->act = RXE_CHANGED;
        }
    } else if (same(fn, "PROJECTPLAY")) {
        if (sw->nfx) o->act = RXE_PLAY;
    } else if (same(fn, "PROJECTSTOP") || same(fn, "CROUTONSTOP")) {
        o->act = RXE_STOP;
    } else if (same(fn, "PROJECTUPDATE")) {
        o->act = RXE_CHANGED;
    } else if (same(fn, "CROUTONLOAD")) {
        int at = argc == 2 && same(argv[1], "INSERT") && cur ? sw->fxsel + 1 : sw->nfx;
        if (argc < 1) {
            fail(o, ERR_BADARGNUM);
        } else if (!sw->fx || add_crouton(sw, at, argv[0], NULL, 0, st->ovl)) {
            fail(o, ERR_FNRETURN);
        } else {
            pick(st, o, at);
            say(o, "1");
        }
    } else if (same(fn, "CROUTONDELETE")) {
        if (!cur) {
            fail(o, ERR_FNRETURN);
            return;
        }
        delete_crouton(sw, sw->fxsel, st->ovl);
        deselect(st);           /* the Editor has none selected after it */
        say(o, "0");
        o->act = RXE_CHANGED;
    } else if (same(fn, "CROUTONPICK")) {
        long n = sw->nfx;
        if (argc == 0) {
            deselect(st);
            o->act = RXE_CHANGED;
            say(o, "OK");
            return;
        }
        if (argc >= 2) n = place(argv);
        else if (same(argv[0], "FIRST")) n = 0;
        else if (same(argv[0], "LAST")) n = sw->nfx - 1;
        else if (isdigit((unsigned char)argv[0][0])) n = atol(argv[0]);
        if (!crouton(st, n)) {
            fail(o, ERR_FNRETURN);
            return;
        }
        pick(st, o, n);
        say(o, "OK");
    } else if (same(fn, "CROUTONNAME")) {
        if (argc == 2) pick(st, o, place(argv));
        say(o, "%s", sw->fxsel >= 0 ? sw->fx[sw->fxsel].path : "");
    } else if (same(fn, "CROUTONSELECTED")) {
        say(o, "%d", argc == 2 && sw->fxsel >= 0 && place(argv) == sw->fxsel);
    } else if (same(fn, "CROUTONSPOT")) {
        if (cur) say(o, "%d", sw->fxsel);
        else fail(o, ERR_FNRETURN);
    } else if (same(fn, "CROUTONSINPROJECT")) {
        say(o, "%d", sw->nfx);
    } else if (same(fn, "CROUTONTIME")) {
        long t = 0;
        int k;
        if (!cur) {
            fail(o, ERR_FNRETURN);
            return;
        }
        for (k = 0; k < sw->fxsel; k++) t += duration(st, &sw->fx[k]);
        say(o, "%ld", t * FIELDS / TICKS);
    } else if (same(fn, "SET_VIEW")) {
        long n = argc ? atol(argv[0]) : 0;
        if (argc < 1) {
            fail(o, ERR_BADARGNUM);
            return;
        }
        say(o, "%d", viewmode);
        viewmode = n < 0 || n >= 7 ? 0 : n;
    } else if (same(fn, "GETSCREEN")) {
        say(o, "%lu", (unsigned long)st->w->WScreen);
    } else if (same(fn, "CURRENTPATH") || same(fn, "FLYERDRIVE")) {
        say(o, "");             /* no files window, no Flyer */
    } else if (same(fn, "RECORDCLIP") || same(fn, "RECORDADD")) {
        /* the clip is a file that is made here; the source is Program and the compression the recorder's, whatever
           is asked for. RECORDADD adds to no clip there is, and answers when its fields are written */
        int add = same(fn, "RECORDADD"), r;
        long fields = argc > 1 ? atol(argv[1]) : 0;
        if (argc < 1 + add) {
            fail(o, ERR_BADARGNUM);
            return;
        }
        r = add && fields <= 0 ? REC_BADPARAM : rec_start(argv[0], fields);
        if (r < 0) fail(o, ERR_FNRETURN);       /* nothing records */
        else if (add && r == REC_OKAY) o->act = RXE_RECORD;
        else say(o, "%d", add && r == REC_EXISTS ? REC_CANTEXTEND : r);
    } else if (same(fn, "RECORDPAUSE")) {
        if (argc < 1) fail(o, ERR_BADARGNUM);
        else rec_pause(atol(argv[0]) == 1);
    } else if (same(fn, "RECORDSTOP")) {
        rec_stop();
    } else if (same(fn, "FLYERSTATUS")) {
        if (rec_status() < 0) fail(o, ERR_FNRETURN);
        else say(o, "%d", rec_status());
    } else {                    /* another library's function */
        o->rc = RC_WARN;
        o->rc2 = ERR10_001;
    }
}
