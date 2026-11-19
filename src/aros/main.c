/* Video Toaster switcher for AROS: the interface drawn from the user's installed Switcher.main.AA, with the
 * Toaster card replaced by input backends (VHI) and a software mixer.
 * Switcher [FILE=<Switcher.main.AA>] [SCALE=<n>] [IN1=..] [IN2=..] [IN3=..] [IN4=..] [DV1=..] [DV2=..] [STATS]
 *          [PROJECT=<Toaster project file>]
 * An input is "backend:arg:source", e.g. vhi:uvc:1; without one it comes from ENV:VideoToaster/Inputs
 * (the Inputs window: right mouse button on an input button, or the I key), else it is assigned automatically. */
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <proto/timer.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <cybergraphx/cybergraphics.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "switcher.h"
#include "inputs.h"
#include "inputswin.h"
#include "framestore.h"
#include "controlswin.h"
#include "audioout.h"
#include "gfxfile.h"
#include "swproject.h"
#include "rexxhost.h"
#include "rexxcmd.h"
#include "rexxedit.h"
#include "rexxreq.h"
#include "cgprog.h"
#include "record.h"
#include "outputs.h"
#include "plugins.h"
#include "croutontags.h"
#include <stdio.h>
#include <sys/time.h>

/* sw->rgb and the monitor frames hold native 0x00RRGGBB words */
#if AROS_BIG_ENDIAN
#define RGBFMT RECTFMT_0RGB32
#else
#define RGBFMT RECTFMT_BGR032
#endif

#define MONW 480
#define MONH 360
/* AUTO length in 1/50 s for S, M and F, until effects carry their own */
static const int speedticks[3] = { 100, 50, 25 };
#define MIXMAX 1024             /* Program mix: 0 = Main, MIXMAX = Preview */
/* LDCG and DPCG are answered when the CG's port is there and when it has gone, in 1/50 s at most */
#define CGLOAD (60 * 50)
#define CGUNLOAD (10 * 50)

/* An ARexx crouton in a sequence: its script with the arguments its CommandLine tag holds, on its own. */
static void run_script(struct rexxhost *h, const struct effect *e)
{
    char cmd[800];
    uint32_t n = 0;
    const uint8_t *args = NULL;
    if (!effect_tag(e, TAG_TABLE(TAGN_CommandLine), &n, &args)) n = 0;
    snprintf(cmd, sizeof(cmd), "%s %.*s", e->path, n > 500 ? 500 : (int)n, n ? (const char *)args : "");
    rexx_run(h, cmd);
}

/* The file requester that is up and what it is for. One at a time; the main loop steps it, so the switcher goes
 * on meanwhile. */
enum { FR_CROUTONS = 1, FR_PROJECT, FR_STILL_SAVE, FR_STILL_LOAD, FR_REXX };
static struct filereq *freq;
static int freqfor, freqslot;

static int ask_file(struct Window *w, int what, int slot, const char *title, const char *dir, const char *file,
                    int flags)
{
    if (freq || !(freq = req_file_open(w, title, dir, file, flags))) {
        DisplayBeep(w->WScreen);
        return 0;
    }
    freqfor = what;
    freqslot = slot;
    return 1;
}

/* Tools: croutons from the effects, after the selected one. */
static void ask_croutons(struct Window *w)
{
    ask_file(w, FR_CROUTONS, 0, "Add croutons", "Toaster:Effects", "", REQF_MULTI);
}

static void add_croutons(struct switcher *sw, int *ovl)
{
    int at = sw->fxsel >= 0 ? sw->fxsel + 1 : sw->nfx, first = -1, k;
    char path[256];
    for (k = 0; req_file_name(freq, k, path, sizeof(path)); k++) {
        size_t n = strlen(path);
        if (n > 2 && !strcmp(path + n - 2, ".i")) path[n - 2] = 0;         /* the icon file names it too */
        if (!add_crouton(sw, at, path, NULL, 0, ovl)) {
            if (first < 0) first = at;
            at++;
        }
    }
    if (first >= 0) {
        select_effect(sw, first, *ovl);
        show_selected(sw);
    }
}

static void ask_project(struct Window *w)
{
    char dir[256];
    strncpy(dir, projpath, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = 0;
    *PathPart((STRPTR)dir) = 0;
    ask_file(w, FR_PROJECT, 0, "Save project", dir, base_name(projpath), REQF_SAVE);
}

/* Save slot s's still, or load a picture into it, through a file requester. */
static void ask_still(struct Window *w, int s, int save)
{
    char name[108];
    snprintf(name, sizeof(name), "Still%d.ilbm", s + 1);
    ask_file(w, save ? FR_STILL_SAVE : FR_STILL_LOAD, s, save ? "Save still" : "Load still", "RAM:", name,
             save ? REQF_SAVE : 0);
}

static void still_file(struct framestore *fs, int s, int save, const char *path)
{
    uint8_t *buf;
    size_t n;
    if (save && fs->still[s] && !ilbm_encode24(fs->still[s], fs->w, fs->h, &buf, &n)) {
        BPTR fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
        if (fh) {
            Write(fh, buf, n);
            Close(fh);
        }
        free(buf);
    } else if (!save) {
        LONG len;
        uint8_t *fb = readfile(path, &len);
        struct picture pic;
        if (fb && !ilbm_decode(fb, len, &pic)) {
            if (!fs_load(fs, s, pic.rgb, pic.w, pic.h)) fs->cur = s;
            picture_free(&pic);
        }
        free(fb);
    }
}

static int workbench_front(struct Window *w)
{
    struct Screen *wb = LockPubScreen((CONST_STRPTR)"Workbench");
    if (!wb) return 0;
    UnlockPubScreen(NULL, wb);
    if (w->WScreen == wb) WindowToBack(w);
    return WBenchToFront() ? 1 : 0;
}

static void framestore_lights(struct switcher *sw, const struct framestore *fs)
{
    sw->translit = (fs->frozen ? 1u << TRANS_FREEZE : 1u << TRANS_LIVE) | 1u << (TRANS_FS1 + fs->cur) |
                   (fs->stable[fs->cur] ? 1u << TRANS_STABILIZE : 0);
}

/* FRAMESTORE buttons. FREEZE grabs DV1's live frame (dvbuf) into the current slot and holds it; LIVE releases DV1;
 * 1-4 pick the slot (shown at once while frozen); STABILIZE line-doubles the slot's still; SAVE writes it. */
static void framestore_button(struct switcher *sw, struct framestore *fs, struct inputs *in, struct Window *w, int i,
                              uint32_t *dvbuf)
{
    if (i == TRANS_FREEZE) {
        if (!inputs_grab(in, SLOT_DV1, dvbuf)) fs_freeze(fs, dvbuf);
    } else if (i == TRANS_LIVE) {
        fs_live(fs);
    } else if (i >= TRANS_FS1 && i < TRANS_FS1 + FS_SLOTS) {
        fs->cur = i - TRANS_FS1;
    } else if (i == TRANS_STABILIZE) {
        fs->stable[fs->cur] = !fs->stable[fs->cur];
    } else if (i == TRANS_SAVE) {
        ask_still(w, fs->cur, 1);
    }
    framestore_lights(sw, fs);
}

/* A scroll or crawl of the CG moves by the clock from when it went to air, in fields (59.94 a second). */
static struct timeval pagestart;
static void page_start(void) { gettimeofday(&pagestart, NULL); }
static long page_fields(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return (long)(((now.tv_sec - pagestart.tv_sec) * 1000000.0 + (now.tv_usec - pagestart.tv_usec)) * 59.94 / 1000000.0);
}

/* The selected overlay goes to air or comes off it (overlay_done); a scroll or crawl plays from its start. */
static int overlay_switch(struct switcher *sw, int onair)
{
    onair = overlay_done(sw, onair);
    if (onair >= 0 && effect_moves(&sw->fx[onair])) page_start();
    return onair;
}

/* A frame page of the CG was chosen: its picture goes to the input that is not on Main (the Framestore on DV1, or
 * DV2), and that input to Preview. */
static void frame_page(struct switcher *sw, struct framestore *fs, struct effect *e)
{
    int slot = sw->row[ROW_MAIN].hilite == SLOT_DV1 ? SLOT_DV2 : SLOT_DV1;
    uint32_t *rgb = cgpage_frame(e->page, MONW, MONH);
    effect_free_frames(e);
    if (!rgb) return;
    fs_page(fs, rgb, slot == SLOT_DV1);
    sw->row[ROW_PRVW].hilite = slot;
}
#define FRAMEMICROS 20000
#define SEQHOLD 100             /* Play: a crouton holds 2 s after its transition unless its Controls say otherwise */       /* monitor refresh during a transition */

static const char version[] __attribute__((used)) = "$VER: Switcher 0.8 (19.11.2026)";

/* Put a rectangle of the interface display (all of it when rw is 0) into the window. */
static void show_rect(struct Window *w, struct switcher *sw, int scale, uint32_t *sbuf, int rx, int ry, int rw, int rh)
{
    int y, x, k;
    if (!rw) { rx = ry = 0; rw = DISPLAYWIDTH; rh = DISPLAYHEIGHT; }
    if (scale == 1) {
        WritePixelArray(sw->rgb, rx, ry, DISPLAYWIDTH * 4, w->RPort, w->BorderLeft + rx, w->BorderTop + ry, rw, rh,
                        RGBFMT);
        return;
    }
    for (y = ry; y < ry + rh; y++) {
        uint32_t *d = sbuf + (size_t)y * scale * DISPLAYWIDTH * scale;
        for (x = rx; x < rx + rw; x++)
            for (k = 0; k < scale; k++)
                d[x * scale + k] = sw->rgb[y * DISPLAYWIDTH + x];
        for (k = 1; k < scale; k++)
            memcpy(d + (size_t)k * DISPLAYWIDTH * scale + rx * scale, d + rx * scale, rw * scale * 4);
    }
    WritePixelArray(sbuf, rx * scale, ry * scale, DISPLAYWIDTH * scale * 4, w->RPort, w->BorderLeft + rx * scale,
                    w->BorderTop + ry * scale, rw * scale, rh * scale, RGBFMT);
}

static void show(struct Window *w, struct switcher *sw, int scale, uint32_t *sbuf)
{
    show_rect(w, sw, scale, sbuf, 0, 0, 0, 0);
}

/* Program = Main, or the transition from Main to Preview at t/MIXMAX: the current effect's wipe frame where it has
 * one (each mask value shows Main, Preview or the matte, see effect.h), else a dissolve. */
static LONG ticks_now(void);
static volatile int audiomix;           /* the transition's position for the sound thread */
static volatile int audiolevel[2];      /* Program's left and right peak, from the sound thread */

/* Key overlay e over buf at level 0..MIXMAX; animated overlays loop at 30 frames a second, and a CG page that
 * moves is where it is fields after its start. */
static void key_overlay(uint32_t *buf, const struct effect *e, int level, LONG now, long fields)
{
    int f = e->nframes > 1 ? (int)((now * 30 / 50) % e->nframes) : 0, x, y;
    uint32_t matte = e->maincolor >= 1 && e->maincolor <= 8 ? (uint32_t)sw_bgcolor(e->maincolor) : 0;
    if (e->page) {
        long pos2 = 0;
        if (cgpage_at(e->page, fields, &pos2)) cgpage_blend(e->page, pos2, buf, MONW, MONH, level, MIXMAX);
        return;
    }
    if (level <= 0 || !e->masks) return;
    for (y = 0; y < MONH; y++)
        for (x = 0; x < MONW; x++) {
            uint32_t c, o = 0, *p = &buf[y * MONW + x];
            int a = effect_overlay_px(e, f, x * FRAME_W / MONW, y * FRAME_H / MONH, matte, &c) * level / MIXMAX, ch;
            if (!a) continue;
            for (ch = 0; ch < 24; ch += 8)
                o |= ((((*p >> ch) & 255) * (255 - a) + ((c >> ch) & 255) * a) / 255) << ch;
            *p = o;
        }
}

/* Program's sound level: two bars at the right edge of the Program monitor, -60..0 dBFS, green to -18, yellow to
 * -6, red above, each with its peak falling back slowly. */
static void draw_meters(uint32_t *buf)
{
    static double held[2];
    int c, x, y;
    for (c = 0; c < 2; c++) {
        double v = audiolevel[c] / 32767.0, db = v > 0.001 ? 20 * log10(v) : -60;
        int h, x0 = MONW - 16 + c * 7;
        if (db < -60) db = -60;
        held[c] = db > held[c] ? db : held[c] - 0.5;
        if (held[c] < -60) held[c] = -60;
        h = (int)((db + 60) / 60 * (MONH - 16));
        for (y = 8; y < MONH - 8; y++)
            for (x = x0; x < x0 + 5; x++) {
                double ydb = (double)(MONH - 8 - y) / (MONH - 16) * 60 - 60;
                int lit = MONH - 8 - y <= h, peak = fabs(ydb - held[c]) < 0.6;
                uint32_t col = ydb > -6 ? 0xe02020 : ydb > -18 ? 0xe0d020 : 0x20d040;
                if (lit || peak) buf[y * MONW + x] = col;
                else buf[y * MONW + x] = (buf[y * MONW + x] >> 2) & 0x3f3f3f;   /* the scale's track, dimmed */
            }
    }
}

/* Program and Preview. The selected effect runs between Main and Preview at t; an overlay on air (onair) is keyed
 * over the result like a downstream key, and a selected overlay instead fades itself on (or, when it is the one on
 * air, off) with t. */
/* Superimpose: the Key bus keyed over Program on its luminance, out where it is darker (CLIP_BLACK) or lighter
 * (CLIP_WHITE) than the clip level, with an edge 16 levels wide. */
static void lum_key(uint32_t *buf, const uint32_t *key, int mode, int clip)
{
    int i;
    for (i = 0; i < MONW * MONH; i++) {
        uint32_t k = key[i], o = 0;
        int l = (int)(((k >> 16) & 255) * 77 + ((k >> 8) & 255) * 150 + (k & 255) * 29) >> 8, ch;
        int a = (mode == CLIP_BLACK ? l - clip : clip - l) * 255 / 16 + 128;
        if (a <= 0) continue;
        if (a >= 255) { buf[i] = k; continue; }
        for (ch = 0; ch < 24; ch += 8)
            o |= ((((buf[i] >> ch) & 255) * (255 - a) + ((k >> ch) & 255) * a) / 255) << ch;
        buf[i] = o;
    }
}

static void show_monitors(struct Window *m, const struct switcher *sw, uint32_t *mainbuf, uint32_t *prvwbuf,
                          uint32_t *progbuf, const uint32_t *keybuf, int t, int onair)
{
    const struct effect *fx = FXCUR(sw), *ov = onair >= 0 ? &sw->fx[onair] : NULL;
    int i, ovlevel = MIXMAX, sel = -1;
    LONG now = ticks_now();
    if (!m && !rec_active() && !out_active()) return;
    if (fx && fx->overlay) {
        if (sw->fxsel == onair) ovlevel = MIXMAX - t;
        else sel = t;
        fx = NULL;
        t = 0;
    }
    if (t > 0 && fx && fx->algo) {
        algofx_render(fx->algo, t, MIXMAX, mainbuf, prvwbuf, progbuf, MONW, MONH, 0);
    } else if (t > 0 && fx && fx->masks && fx->nframes) {
        int f = fx->wipemap ? 0 : t * (fx->nframes - 1) / MIXMAX, x, y;
        for (y = 0; y < MONH; y++) {
            int my = y * fx->h / MONH;
            for (x = 0; x < MONW; x++) {
                int v = effect_mask(fx, f, x * fx->w / MONW, my), r = fx->role[v];
                uint32_t a = mainbuf[y * MONW + x], b = prvwbuf[y * MONW + x];
                if (fx->wipemap) {
                    int l = 0, c, rr = effect_picture(fx, x * FRAME_W / MONW, y * FRAME_H / MONH, t, MIXMAX, &l, &progbuf[y * MONW + x]);
                    if (fx->wipemap == PIC_MAP) {
                        uint32_t o = 0;
                        for (c = 0; c < 24; c += 8)
                            o |= (uint32_t)((((a >> c) & 255) * (255 - l) + ((b >> c) & 255) * l) / 255) << c;
                        progbuf[y * MONW + x] = o;
                    } else if (rr != FXR_COLOR) {
                        progbuf[y * MONW + x] = rr == FXR_B ? b : a;
                    }
                } else if (fx->soft) {
                    int l = fx->level[v], c;
                    uint32_t o = 0;
                    for (c = 0; c < 24; c += 8)
                        o |= (uint32_t)((((a >> c) & 255) * (255 - l) + ((b >> c) & 255) * l) / 255) << c;
                    progbuf[y * MONW + x] = o;
                } else {
                    progbuf[y * MONW + x] = r == FXR_B ? b : r == FXR_COLOR ? fx->color[v] : a;
                }
            }
        }
    } else if (t > 0) {
        for (i = 0; i < MONW * MONH; i++) {
            uint32_t a = mainbuf[i], b = prvwbuf[i], r = 0;
            int c;
            for (c = 0; c < 24; c += 8) {
                int va = (a >> c) & 255, vb = (b >> c) & 255;
                r |= (uint32_t)(va + (vb - va) * t / MIXMAX) << c;
            }
            progbuf[i] = r;
        }
    } else {
        memcpy(progbuf, mainbuf, MONW * MONH * 4);
    }
    if (sw->row[ROW_LUMKEY].hilite >= 0 && sw->row[ROW_CLIP].hilite != CLIP_OFF)
        lum_key(progbuf, keybuf, sw->row[ROW_CLIP].hilite, sw->keyclip);
    if (ov) key_overlay(progbuf, ov, ovlevel, now, page_fields());
    if (sel > 0) key_overlay(progbuf, FXCUR(sw), sel, now, 0);     /* a scroll or crawl starts outside the frame */
    rec_video(progbuf);
    out_video(progbuf);
    if (!m) return;
    draw_meters(progbuf);
    WritePixelArray(progbuf, 0, 0, MONW * 4, m->RPort, m->BorderLeft, m->BorderTop, MONW, MONH, RGBFMT);
    WritePixelArray(prvwbuf, 0, 0, MONW * 4, m->RPort, m->BorderLeft + MONW + 8, m->BorderTop, MONW, MONH, RGBFMT);
}

/* Sub labels under the input buttons: each input's name or stream, the colour on an unnamed BKG. */
static void set_labels(struct switcher *sw, struct inputs *in, const struct inputprefs *p, const struct framestore *fs)
{
    static const char *const still[FS_SLOTS] = { "Still 1", "Still 2", "Still 3", "Still 4" };
    int s;
    for (s = 0; s < NSLOTS && s < 8; s++) {
        const char *n = s == SLOT_BKG && !p->nick[s][0] ? sw_bgname(sw->bgcolor) :
                        s == SLOT_DV1 && fs->frozen ? still[fs->cur] : inputprefs_label(p, in, s);
        strncpy(sw->inlabel[s], n, sizeof(sw->inlabel[s]) - 1);
        sw->inlabel[s][sizeof(sw->inlabel[s]) - 1] = 0;
    }
}

static void title_monitors(struct Window *m, struct inputs *in, const struct inputprefs *p, int mainslot, int prvwslot)
{
    static char title[160];
    if (!m) return;
    strcpy(title, "Program: ");
    strncat(title, p->nick[mainslot][0] ? p->nick[mainslot] : inputs_slot_name(in, mainslot), 60);
    strcat(title, "   Preview: ");
    strncat(title, p->nick[prvwslot][0] ? p->nick[prvwslot] : inputs_slot_name(in, prvwslot), 60);
    SetWindowTitles(m, (CONST_STRPTR)title, (CONST_STRPTR)~0);
}

/* The BKG bus is the Toaster's colour generator: a matte, or SNOW (noise). */
static void fill_bkg(const struct switcher *sw, uint32_t *buf, int slot)
{
    static uint32_t seed = 0x2545f491;
    long c = sw_bgcolor(sw->bgcolor);
    int i;
    if (slot != SLOT_BKG) return;
    for (i = 0; i < MONW * MONH; i++) {
        if (c < 0) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            buf[i] = (seed & 0xff) * 0x010101u;
        } else {
            buf[i] = (uint32_t)c;
        }
    }
}

static LONG ticks_now(void)
{
    static LONG day0 = -1;
    struct DateStamp ds;
    DateStamp(&ds);
    if (day0 < 0) day0 = ds.ds_Days;
    return (ds.ds_Days - day0) * (24 * 60 * 60 * 50) + ds.ds_Minute * 60 * 50 + ds.ds_Tick;
}

enum { A_FILE, A_SCALE, A_IN1, A_IN2, A_IN3, A_IN4, A_DV1, A_DV2, A_STATS, A_PROJECT, A_CG, NARGS };

int main(void)
{
    SIPTR args[NARGS];
    struct RDArgs *rda;
    const char *file, *specs[NSLOTS];
    int scale = 0, done = 0, rc = RETURN_FAIL, sigbit = -1, s, mix = 0, automix = 0, autoticks = 0, picks;
    int tbdown = 1, dragging = 0, grab = 0, tbx = 0, tby = 0, tbw = 0, tbh = 0, tbarupdate_pending = 0;
    int ovl = -1;                       /* the overlay on air (keyed over Program), or -1 */
    int monback = 0;                    /* the monitors window is behind the Toaster window */
    int seqpos = -1;                    /* Play: the next crouton the sequence runs, -1 when stopped */
    LONG seqnext = 0;                   /* when it runs */
    struct framestore fs;
    struct audioout *ao = NULL;
    LONG autostart = 0;
    struct switcher sw;
    struct Screen *scr;
    struct Window *w = NULL, *mon = NULL;
    struct inputs *in = NULL;
    struct inputswin *iw = NULL;
    struct inputprefs prefs;
    uint32_t *sbuf = NULL, *mainbuf = NULL, *prvwbuf = NULL, *progbuf = NULL, *keybuf = NULL, *dvbuf = NULL;
    uint32_t seq = 0, nframes = 0;
    int named = 0;
    LONG statstart = 0;
    struct MsgPort *tport = NULL;
    struct timerequest *treq = NULL;
    int tpending = 0;
    struct rexxhost *rxsw = NULL;       /* ARexx: the Switcher's functions */
    struct rexxhost *rxfrom = NULL;     /* the port of the call that waits for its answer (rxcall, rxo) */
    struct rexxcall rxcall;
    struct rxout rxo;
    struct rexxhost *rxed = NULL;       /* the Editor's functions; a call of theirs waits in rxecall and rxe */
    struct rexxcall rxecall;
    struct rxeout rxe;
    int rxewaits = 0;                   /* 1: for its answer, 2: for the sequence it started to end */
    int rxrow = -1, rxindex = 0;        /* a button an ARexx call presses */
    int cgwait = 0;                     /* its answer waits for the CG: 1 to be loaded, 2 to have gone */
    long cguntil = 0;
    const char *cgnamed;                /* CG=: the titler's program */
    int autoto = MIXMAX;                /* where the running transition ends: MIXMAX, or 0 for one taken back */
    long framebase;
    uint8_t *exe;
    LONG len;

    memset(args, 0, sizeof(args));
    memset(&sw, 0, sizeof(sw));
    rda = ReadArgs((CONST_STRPTR)"FILE,SCALE/N,IN1/K,IN2/K,IN3/K,IN4/K,DV1/K,DV2/K,STATS/S,PROJECT/K,CG/K", args, NULL);
    cgnamed = (const char *)args[A_CG];
    file = args[A_FILE] ? (const char *)args[A_FILE] : "Toaster:Programs/Switcher.main.AA";
    if (args[A_SCALE]) scale = *(LONG *)args[A_SCALE];
    inputprefs_default(&prefs);
    inputprefs_load(&prefs, "ENV:VideoToaster/Inputs");
    for (s = 0; s < NSLOTS; s++) specs[s] = prefs.has[s] ? prefs.spec[s] : NULL;
    for (s = SLOT_IN1; s <= SLOT_DV2; s++)
        if (args[A_IN1 + s]) specs[s] = (const char *)args[A_IN1 + s];

    exe = readfile(file, &len);
    if (!exe) {
        Printf((CONST_STRPTR)"Switcher: can't read %s\n", file);
        goto out;
    }
    if ((rc = sw_open(&sw, exe, len))) {
        Printf((CONST_STRPTR)"Switcher: %s is not a Video Toaster switcher (%ld)\n", file, (long)rc);
        rc = RETURN_FAIL;
        free(exe);
        goto out;
    }
    free(exe);
    sw.row[ROW_MAIN].hilite = SLOT_IN1;
    sw.row[ROW_PRVW].hilite = SLOT_IN2;
    if (args[A_PROJECT] && load_project(&sw, (const char *)args[A_PROJECT])) {     /* a new project of that name */
        strncpy(projpath, (const char *)args[A_PROJECT], sizeof(projpath) - 1);
        projpath[sizeof(projpath) - 1] = 0;
    } else if (!args[A_PROJECT]) {
        load_project(&sw, projpath);
    }
    {   /* the Editor bar: NewTek's gadget sheet and interface font */
        static const char *const fonts[] = { "FONTS:InterfaceLG/11", "Toaster:Programs/System_Support/Fonts/InterfaceLG/11.8c" };
        LONG rl;
        uint8_t *rb = readfile("Toaster:Programs/Edit_Support/Gadget64.pic", &rl);
        unsigned k;
        if (rb) sw.bar.have_sheet = !ilbm_decode(rb, rl, &sw.bar.sheet);
        free(rb);
        for (k = 0; k < sizeof(fonts) / sizeof(fonts[0]) && !sw.bar.have_font; k++)
            if ((rb = readfile(fonts[k], &rl))) {
                sw.bar.have_font = !colorfont_load(&sw.bar.font, rb, rl);
                free(rb);
            }
    }
    select_effect(&sw, 0, -1);

    mainbuf = calloc(MONW * MONH, 4);
    prvwbuf = calloc(MONW * MONH, 4);
    progbuf = calloc(MONW * MONH, 4);
    keybuf = calloc(MONW * MONH, 4);
    dvbuf = calloc(MONW * MONH, 4);
    fs_init(&fs, MONW, MONH);
    rec_init(MONW, MONH);
    out_init(MONW, MONH);
    plugins_init();
    plugins_scan();
    sw.translit = 1u << TRANS_LIVE | 1u << TRANS_FS1;
    if (!mainbuf || !prvwbuf || !progbuf || !keybuf || !dvbuf || (sigbit = AllocSignal(-1)) < 0) goto close;
    in = inputs_start(specs, MONW, MONH, sigbit);
    if (!in) {
        PutStr((CONST_STRPTR)"Switcher: can't start the capture process\n");
        goto close;
    }
    inputs_route(in, sw.row[ROW_MAIN].hilite, sw.row[ROW_PRVW].hilite);
    for (s = 0; s < NSLOTS; s++) inputs_sound(in, s, prefs.sound[s]);
    ao = audio_start(in, &audiomix, MIXMAX, audiolevel);     /* sound follows Program; none without AHI */
    inputprefs_start_outputs(&prefs);
    if (!(tport = CreateMsgPort()) || !(treq = (struct timerequest *)CreateIORequest(tport, sizeof(*treq))) ||
        OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, &treq->tr_node, 0)) {
        if (treq) DeleteIORequest(&treq->tr_node);
        treq = NULL;
        goto close;
    }
    statstart = framebase = ticks_now();

    scr = LockPubScreen(NULL);
    if (!scale)     /* 2x when the Toaster and the Program/Preview monitors fit side by side */
        scale = scr && scr->Width >= DISPLAYWIDTH * 2 + MONW * 2 + 8 + 64 && scr->Height >= DISPLAYHEIGHT * 2 + 64 ? 2 : 1;
    w = OpenWindowTags(NULL, WA_Title, (IPTR)"Video Toaster", WA_InnerWidth, DISPLAYWIDTH * scale,
                       WA_InnerHeight, DISPLAYHEIGHT * scale, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                       WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_PubScreen, (IPTR)scr,
                       WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY |
                       IDCMP_REFRESHWINDOW, WA_ReportMouse, TRUE, WA_SimpleRefresh, TRUE, TAG_DONE);
    sw_box(&sw, ROW_TBAR, TBAR_FG, &tbx, &tby, &tbw, &tbh);
    mon = OpenWindowTags(NULL, WA_Title, (IPTR)"Program / Preview", WA_InnerWidth, MONW * 2 + 8, WA_InnerHeight, MONH,
                         WA_Left, w && w->LeftEdge + w->Width + 4 + MONW * 2 + 8 + 24 <= (scr ? scr->Width : 0) ?
                                  w->LeftEdge + w->Width + 4 : (scr ? scr->Width - (MONW * 2 + 8) - 24 : 0),
                         WA_Top, scr ? scr->BarHeight + 1 : 0,
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_PubScreen, (IPTR)scr, WA_IDCMP, IDCMP_REFRESHWINDOW,
                         WA_SimpleRefresh, TRUE, TAG_DONE);
    if (mon && w && mon->LeftEdge < w->LeftEdge + w->Width && mon->TopEdge < w->TopEdge + w->Height) {
        WindowToBack(mon);              /* no room beside the Toaster: behind it, M brings it forward */
        monback = 1;
    }
    if (scr) UnlockPubScreen(NULL, scr);
    if (!w) {
        PutStr((CONST_STRPTR)"Switcher: can't open the window\n");
        goto close;
    }
    if (scale > 1 && !(sbuf = malloc((size_t)DISPLAYWIDTH * scale * DISPLAYHEIGHT * scale * 4)))
        goto close;
    sw_render(&sw);
    show(w, &sw, scale, sbuf);
    rxsw = rexx_open("ToasterARexx.port");
    rxed = rexx_open("PROJECT_REXX_PORT");

    picks = sw.fxpicks;
    while (!done) {
        struct IntuiMessage *m;
        ULONG mask = 1UL << w->UserPort->mp_SigBit | (mon ? 1UL << mon->UserPort->mp_SigBit : 0) | 1UL << sigbit |
                     inputswin_sigmask(iw) | 1UL << tport->mp_SigBit | rexx_sigmask(rxsw) | rexx_sigmask(rxed) |
                     req_file_sigmask(freq);
        ULONG got = req_file_nowait(freq) ? SetSignal(0, mask) & mask : Wait(mask);
        int redraw = 0, changed = 0, tick = 0, tbarupdate = tbarupdate_pending;

        if (tpending && CheckIO(&treq->tr_node)) {
            WaitIO(&treq->tr_node);
            tpending = 0;
            tick = 1;
        }

        if (iw && (got & inputswin_sigmask(iw)) && !inputswin_handle(iw, &changed))
            iw = NULL;
        if (iw) inputswin_poll(iw);
        if (changed) {
            title_monitors(mon, in, &prefs, sw.row[ROW_MAIN].hilite, sw.row[ROW_PRVW].hilite);
            set_labels(&sw, in, &prefs, &fs);
            redraw = 1;
        }

        tbarupdate_pending = 0;
        if ((got & (1UL << sigbit)) || tick || tbarupdate) {
            uint32_t n = inputs_frames(in, mainbuf, prvwbuf);
            fill_bkg(&sw, mainbuf, sw.row[ROW_MAIN].hilite);       /* BKG: the colour generator, not an input */
            fill_bkg(&sw, prvwbuf, sw.row[ROW_PRVW].hilite);
            if (sw.row[ROW_LUMKEY].hilite >= 0) {
                inputs_key(in, keybuf);
                fill_bkg(&sw, keybuf, sw.row[ROW_LUMKEY].hilite);
            }
            if (fs.frozen) {                    /* the Framestore holds DV1 */
                if (sw.row[ROW_MAIN].hilite == SLOT_DV1) fs_show(&fs, mainbuf);
                if (sw.row[ROW_PRVW].hilite == SLOT_DV1) fs_show(&fs, prvwbuf);
                if (sw.row[ROW_LUMKEY].hilite == SLOT_DV1) fs_show(&fs, keybuf);
            }
            if (fs.dv2) {                       /* and a frame page DV2 */
                if (sw.row[ROW_MAIN].hilite == SLOT_DV2) memcpy(mainbuf, fs.dv2, MONW * MONH * 4);
                if (sw.row[ROW_PRVW].hilite == SLOT_DV2) memcpy(prvwbuf, fs.dv2, MONW * MONH * 4);
                if (sw.row[ROW_LUMKEY].hilite == SLOT_DV2) memcpy(keybuf, fs.dv2, MONW * MONH * 4);
            }
            if (named < 2 && (inputs_ready(in) || (!named && n != seq))) {   /* at the first picture, and once all */
                named = inputs_ready(in) ? 2 : 1;                           /* the inputs have been opened */
                title_monitors(mon, in, &prefs, sw.row[ROW_MAIN].hilite, sw.row[ROW_PRVW].hilite);
                set_labels(&sw, in, &prefs, &fs);
                redraw = 1;
            }
            if (n != seq) {
                nframes += n - seq;
                seq = n;
            }
            if (args[A_STATS] && ticks_now() - statstart >= 250) {
                {
                    char st[512];
                    Printf((CONST_STRPTR)"frames %lu in 5 s\n", (unsigned long)nframes);
                    if (inputs_stats(in, st, sizeof(st))) PutStr((CONST_STRPTR)st);
                }
                nframes = 0;
                statstart = ticks_now();
            }
            if (autostart) {                    /* AUTO: run the T-bar to its end */
                LONG el = ticks_now() - autostart;
                mix = el >= autoticks ? autoto : automix + (autoto - automix) * el / autoticks;
                if (mix == autoto) {
                    autostart = 0;
                    autoto = MIXMAX;
                }
                tbarupdate = 1;
            }
            if (tbarupdate) {
                tbarupdate = 0;
                sw.tbarpos = (tbdown ? mix : MIXMAX - mix) * sw.tbartravel / MIXMAX;
                if (mix == MIXMAX) {            /* transition done: Preview is the new Main, the T-bar stays */
                    if (FXCUR(&sw) && FXCUR(&sw)->overlay) {
                        ovl = overlay_switch(&sw, ovl);
                    } else {
                        int t = sw.row[ROW_MAIN].hilite;
                        sw.row[ROW_MAIN].hilite = sw.row[ROW_PRVW].hilite;
                        sw.row[ROW_PRVW].hilite = t;
                        inputs_route(in, sw.row[ROW_MAIN].hilite, sw.row[ROW_PRVW].hilite);
                        title_monitors(mon, in, &prefs, sw.row[ROW_MAIN].hilite, sw.row[ROW_PRVW].hilite);
                        inputs_frames(in, mainbuf, prvwbuf);
                    }
                    tbdown = !tbdown;
                    mix = 0;
                    dragging = 0;
                    redraw = 1;
                } else {
                    sw_render(&sw);
                    show_rect(w, &sw, scale, sbuf, tbx, tby, tbw, tbh);
                }
            }
            if (ovl >= 0 && sw.fx[ovl].page && effect_moves(&sw.fx[ovl]) && !mix && !autostart) {
                long pos2;                      /* a scroll or crawl played once comes off by itself */
                if (!cgpage_at(sw.fx[ovl].page, page_fields(), &pos2)) {
                    if (ovl != sw.fxsel) effect_free_frames(&sw.fx[ovl]);
                    ovl = -1;
                }
            }
            show_monitors(mon, &sw, mainbuf, prvwbuf, progbuf, keybuf, mix, ovl);
            audiomix = FXCUR(&sw) && FXCUR(&sw)->overlay ? 0 : mix;    /* an overlay leaves the sound alone */
        }
        if (seqpos >= 0 && !autostart && !dragging && ticks_now() >= seqnext) {   /* Play: the next crouton */
            if (seqpos >= sw.nfx) {
                seqpos = -1;
            } else {
                struct effect *e = &sw.fx[seqpos];
                int slot = effect_main_slot(e);
                seqnext = ticks_now() + (e->ctl_hold >= 0 ? e->ctl_hold : SEQHOLD);
                if (e->ctl_hold < 0 && e->type >= FX_KEYF) {    /* a CG page holds as long as it says */
                    uint32_t fields = 0;
                    if (effect_tag(e, TAG_VALUE(TAGN_Duration), &fields, NULL) && fields < 1000000)
                        seqnext = ticks_now() + fields * 50 / 60;
                }
                if (e->type == FX_CTRL) {
                    if (e->ctrl == CTRL_STOP) seqpos = -2;
                } else if (e->type == FX_REXX) {
                    uint32_t delay = 0;         /* in fields */
                    if (e->ctl_hold < 0)
                        seqnext = ticks_now() + (effect_tag(e, TAG_VALUE(TAGN_Delay), &delay, NULL) ? delay * 50 / 60 : 0);
                    run_script(rxed, e);
                } else if (slot >= 0) {
                    sw.row[ROW_MAIN].hilite = slot;
                    if (slot == SLOT_BKG && e->maincolor >= 1 && e->maincolor <= 9) sw.bgcolor = e->maincolor;
                    inputs_route(in, sw.row[ROW_MAIN].hilite, sw.row[ROW_PRVW].hilite);
                    title_monitors(mon, in, &prefs, sw.row[ROW_MAIN].hilite, sw.row[ROW_PRVW].hilite);
                    set_labels(&sw, in, &prefs, &fs);
                } else {
                    select_effect(&sw, seqpos, ovl);
                    show_selected(&sw);
                    autostart = ticks_now();
                    automix = mix;
                    autoticks = effect_ticks(&sw.fx[seqpos], sw.speed, speedticks[sw.speed]) * (MIXMAX - mix) / MIXMAX;
                    if (autoticks < 1 || (effect_moves(&sw.fx[seqpos]) && seqpos != ovl)) autoticks = 1;
                    seqnext += autoticks;
                    Signal(FindTask(NULL), 1UL << sigbit);
                }
                seqpos++;
            }
            if (seqpos < 0) seqpos = -1;
            sw.bar.lit = seqpos >= 0 ? 1u << EB_PLAY : 0;
            redraw = 1;
        }
        /* ARexx: one call at a time, between transitions; its answer waits for what it started */
        while (!rxfrom && !autostart && !dragging && rexx_next(rxsw, &rxcall)) {
            struct rxstate st = { &sw, &fs, in, dvbuf, mix, MIXMAX, tbdown, ticks_now(), &framebase, speedticks };
            rxswitcher(&st, rxcall.fn, rxcall.argc, rxcall.argv, &rxo);
            rxfrom = rxsw;
            switch (rxo.rc ? RXA_NONE : rxo.act) {
            case RXA_PRESS:
                rxrow = rxo.row;
                rxindex = rxo.index;
                break;
            case RXA_TBAR:
                mix = rxo.value;
                tbarupdate_pending = 1;
                break;
            case RXA_UATO:
                if (mix) {
                    const struct effect *fx = FXCUR(&sw);
                    autostart = ticks_now();
                    automix = mix;
                    autoto = 0;
                    autoticks = (fx ? effect_ticks(fx, sw.speed, speedticks[sw.speed]) : speedticks[sw.speed]) *
                                mix / MIXMAX;
                    if (autoticks < 1) autoticks = 1;
                }
                break;
            case RXA_TOSW:
                ScreenToFront(w->WScreen);
                WindowToFront(w);
                ActivateWindow(w);
                break;
            case RXA_TOWB:
                if (!workbench_front(w)) rxo.rc = 10;
                break;
            case RXA_CHANGED:
                framestore_lights(&sw, &fs);
                set_labels(&sw, in, &prefs, &fs);
                inputs_route(in, sw.row[ROW_MAIN].hilite, sw.row[ROW_PRVW].hilite);
                inputs_route_key(in, sw.row[ROW_LUMKEY].hilite);
                title_monitors(mon, in, &prefs, sw.row[ROW_MAIN].hilite, sw.row[ROW_PRVW].hilite);
                redraw = 1;
                break;
            case RXA_LDCG:
                if (!cg_loaded()) {
                    char path[256];
                    if (cg_find(cgnamed, path, sizeof(path)) && cg_start(path)) {
                        cgwait = 1;
                        cguntil = ticks_now() + CGLOAD;
                    } else rxo.rc = 10;
                }
                break;
            case RXA_DPCG:
                if (cg_loaded() && rexx_send(rxsw, CG_PORT, CG_UNLOAD)) {
                    cgwait = 2;
                    cguntil = ticks_now() + CGUNLOAD;
                }
                break;
            case RXA_QUIT:
                done = 1;
                break;
            }
            Signal(FindTask(NULL), 1UL << sigbit);
        }
        /* the Editor's: not while a sequence plays */
        while (!rxewaits && seqpos < 0 && !autostart && !dragging && rexx_next(rxed, &rxecall)) {
            struct rxedit st = { &sw, &ovl, w, rxed, speedticks, SEQHOLD };
            rxeditor(&st, rxecall.fn, rxecall.argc, rxecall.argv, &rxe);
            rxewaits = 1;
            switch (rxe.rc ? RXE_NONE : rxe.act) {
            case RXE_PLAY:
                seqpos = 0;
                seqnext = 0;
                sw.bar.lit = 1u << EB_PLAY;
                rxewaits = 2;
                break;
            case RXE_RECORD:
                rxewaits = 3;
                break;
            case RXE_FILEREQ:           /* the script asks for the file while the requester is up */
                rexx_reply(&rxecall, rxe.rc, rxe.rc2, rxe.text);
                rxewaits = 0;
                break;
            }
            redraw = 1;                 /* the project or the selection may have changed */
            Signal(FindTask(NULL), 1UL << sigbit);
        }
        if ((autostart || seqpos >= 0 || rxfrom || rxewaits || rec_active() || out_active() ||
             (ovl >= 0 && effect_moves(&sw.fx[ovl]))) && !tpending) {
                                         /* steady redraws during a dissolve, a scroll or crawl, a recording and for the outputs, wake-ups while playing or while a call waits */
            treq->tr_node.io_Command = TR_ADDREQUEST;
            treq->tr_time.tv_secs = 0;
            treq->tr_time.tv_micro = FRAMEMICROS;
            SendIO(&treq->tr_node);
            tpending = 1;
        }
        while (mon && (m = (struct IntuiMessage *)GetMsg(mon->UserPort))) {
            ReplyMsg(&m->ExecMessage);
            BeginRefresh(mon);
            EndRefresh(mon, TRUE);
            show_monitors(mon, &sw, mainbuf, prvwbuf, progbuf, keybuf, mix, ovl);
        }
        while (rxrow >= 0 || (m = (struct IntuiMessage *)GetMsg(w->UserPort))) {
            int synth = rxrow >= 0;             /* a press from ARexx: the button itself, at no place */
            ULONG cl = synth ? IDCMP_MOUSEBUTTONS : m->Class;
            UWORD code = synth ? SELECTDOWN : m->Code;
            int x = synth ? -1 : (m->MouseX - w->BorderLeft) / scale;
            int y = synth ? -1 : (m->MouseY - w->BorderTop) / scale, r = rxrow, i = rxindex;
            rxrow = -1;
            if (!synth) ReplyMsg(&m->ExecMessage);
            switch (cl) {
            case IDCMP_CLOSEWINDOW:
                done = 1;
                break;
            case IDCMP_RAWKEY:
                if (code == 0x45) done = 1;     /* Esc */
                else if (code == 0x17)          /* I: the Inputs window */
                    iw = inputswin_open(iw, w->WScreen, in, &prefs, iw ? -1 : SLOT_IN1,
                                        sw.bar.have_font ? &sw.bar.font : NULL, scale);
                else if ((code == 0x4c || code == 0x7a) && sw.fxtop > 0) {      /* cursor up, wheel: scroll */
                    sw.fxtop--;
                    redraw = 1;
                } else if ((code == 0x4d || code == 0x7b) && (sw.fxtop + GRID_ROWS) * GRID_COLS < sw.nfx) {
                    sw.fxtop++;
                    redraw = 1;
                }
                else if (code == 0x37 && mon) { /* M: the monitors in front of the Toaster or behind it */
                    if (monback) WindowToFront(mon);
                    else WindowToBack(mon);
                    monback = !monback;
                }
                break;
            case IDCMP_REFRESHWINDOW:
                BeginRefresh(w);
                show(w, &sw, scale, sbuf);
                EndRefresh(w, TRUE);
                break;
            case IDCMP_MOUSEMOVE:
                if (dragging) {
                    int pos = y - grab;
                    if (pos < 0) pos = 0;
                    if (pos > sw.tbartravel) pos = sw.tbartravel;
                    mix = (tbdown ? pos : sw.tbartravel - pos) * MIXMAX / (sw.tbartravel ? sw.tbartravel : 1);
                    Signal(FindTask(NULL), 1UL << sigbit);
                    tbarupdate_pending = 1;
                }
                break;
            case IDCMP_MOUSEBUTTONS:
                if (code == SELECTUP) {
                    dragging = 0;
                    break;
                }
                if (!synth && code == SELECTDOWN && !autostart && sw.tbartravel && x >= tbx && x < tbx + tbw &&
                    y >= tby + sw.tbarpos && y < tby + sw.tbarpos + sw.tbarhandle) {   /* grab the T-bar handle */
                    dragging = 1;
                    grab = y - sw.tbarpos;
                    break;
                }
                if (code == MENUDOWN) {         /* right button on an input button: choose its source */
                    if (sw_hit(&sw, x, y, &r, &i) && (r == ROW_MAIN || r == ROW_PRVW) && i < NSLOTS)
                        iw = inputswin_open(iw, w->WScreen, in, &prefs, i, sw.bar.have_font ? &sw.bar.font : NULL,
                                            scale);
                    else if (r == ROW_TRANS && i >= TRANS_FS1 && i < TRANS_FS1 + FS_SLOTS) {   /* load a still */
                        ask_still(w, i - TRANS_FS1, 0);
                    }
                    break;
                }
                if (!synth && code == SELECTDOWN && !dragging && (i = editbar_hit(&sw.bar, x, y)) >= 0) {
                    int idle = !autostart && !mix && seqpos < 0;
                    if (i == EB_DELETE && idle) delete_crouton(&sw, sw.fxsel, &ovl);
                    else if (i == EB_CLONE && idle) clone_crouton(&sw, sw.fxsel, &ovl);
                    else if ((i == EB_TOOLS || i == EB_PROGRAMS) && idle) {
                        /* what scripts added to them; before it for Tools the files to add as croutons, for
                           Programs the CG when there is one */
                        const char *names[26] = { "Add croutons..." };
                        char cgpath[256];
                        int tools = i == EB_TOOLS, list = tools ? RXL_TOOLS : RXL_PROGRAMS;
                        int cg = !tools && (cg_loaded() || cg_find(cgnamed, cgpath, sizeof(cgpath)));
                        int own = tools + cg, n = rxedit_list(list, names + own, 25), k = 0;
                        struct rxedit st = { &sw, &ovl, w, rxed, speedticks, SEQHOLD };
                        if (cg) names[0] = "CG";
                        if (n) k = req_choose(w, tools ? "Tools" : "Programs", names, n + own);
                        if (tools && k == 0) ask_croutons(w);
                        else if (cg && k == 0) {            /* entered when it is loaded, loaded when not */
                            if (cg_loaded()) rexx_send(rxsw, CG_PORT, CG_SELECT);
                            else if (cg_find(cgnamed, cgpath, sizeof(cgpath))) cg_start(cgpath);
                        }
                        else if (n && k >= own) rxedit_start(&st, list, k - own);
                    }
                    else if (i == EB_SAVE) ask_project(w);
                    else if (i == EB_CONTROLS && idle && sw.fxsel >= 0) {
                        struct effect *e = &sw.fx[sw.fxsel];
                        int sp = e->ctl_speed, hold = e->ctl_hold;
                        if (controls_edit(w, e->name, &sp, &hold, SEQHOLD)) {
                            effect_set_controls(e, sp, hold);
                            if (sp >= 0) sw.speed = sp;
                        }
                    }
                    else if (i == EB_PLAY && seqpos < 0 && sw.nfx) {
                        seqpos = sw.fxsel >= 0 ? sw.fxsel : 0;
                        seqnext = 0;
                        sw.bar.lit = 1u << EB_PLAY;
                        Signal(FindTask(NULL), 1UL << sigbit);
                    } else if (i == EB_STOP) {
                        seqpos = -1;
                        sw.bar.lit = 0;
                    }
                    redraw = 1;
                    break;
                }
                if (!synth && code == SELECTDOWN && !autostart && !dragging && !mix &&
                    (i = sw_hit_crouton(&sw, x, y)) >= 0) {
                    int slot = effect_main_slot(&sw.fx[i]);
                    if (slot < 0) {
                        if (sw.fx[i].type != FX_CTRL) select_effect(&sw, i, ovl);   /* the current effect */
                        redraw = 1;
                        break;
                    }
                    sw.row[ROW_MAIN].hilite = slot;     /* a video source crouton cuts its source to Main */
                    if (slot == SLOT_BKG && sw.fx[i].maincolor >= 1 && sw.fx[i].maincolor <= 9) {
                        sw.bgcolor = sw.fx[i].maincolor;
                        set_labels(&sw, in, &prefs, &fs);
                    }
                    inputs_route(in, sw.row[ROW_MAIN].hilite, sw.row[ROW_PRVW].hilite);
                    title_monitors(mon, in, &prefs, sw.row[ROW_MAIN].hilite, sw.row[ROW_PRVW].hilite);
                    redraw = 1;
                    break;
                }
                if (code != SELECTDOWN || autostart || dragging || (!synth && !sw_hit(&sw, x, y, &r, &i))) break;
                if (r == ROW_MAIN || r == ROW_PRVW) {
                    sw.row[r].hilite = i;
                } else if (r == ROW_TRANS && i >= TRANS_FREEZE && i <= TRANS_SAVE) {
                    framestore_button(&sw, &fs, in, w, i, dvbuf);
                    set_labels(&sw, in, &prefs, &fs);
                } else if (r == ROW_LUMKEY && i < NSLOTS) {        /* Superimpose source, again for none */
                    sw.row[r].hilite = sw.row[r].hilite == i ? -1 : i;
                    inputs_route_key(in, sw.row[r].hilite);
                } else if (r == ROW_CLIP && (i == CLIP_OFF || i == CLIP_BLACK || i == CLIP_WHITE)) {
                    sw.row[r].hilite = i;
                } else if (r == ROW_CLIP && i == CLIP_LEVEL) {     /* upper half up, lower half down */
                    int bx, by, bw, bh;
                    sw_box(&sw, r, i, &bx, &by, &bw, &bh);
                    sw.keyclip += y < by + bh / 2 ? 8 : -8;
                    if (sw.keyclip < 0) sw.keyclip = 0;
                    if (sw.keyclip > 255) sw.keyclip = 255;
                } else if (r == ROW_TRANS && i == TRANS_TAKE) {    /* TAKE: cut, Main and Preview trade places */
                    int t = sw.row[ROW_MAIN].hilite;
                    if (mix) {                  /* a cut finishes a half-done transition */
                        mix = 0;
                        tbdown = !tbdown;
                        sw.tbarpos = tbdown ? 0 : sw.tbartravel;
                    }
                    if (FXCUR(&sw) && FXCUR(&sw)->overlay) {    /* an overlay is cut on or off instead */
                        ovl = overlay_switch(&sw, ovl);
                        redraw = 1;
                        Signal(FindTask(NULL), 1UL << sigbit);
                        break;
                    }
                    sw.row[ROW_MAIN].hilite = sw.row[ROW_PRVW].hilite;
                    sw.row[ROW_PRVW].hilite = t;
                } else if (r == ROW_TRANS && i == TRANS_AUTO) {    /* AUTO: run the T-bar to its end */
                    autostart = ticks_now();
                    automix = mix;
                    {
                        const struct effect *fx = FXCUR(&sw);
                        autoticks = (fx ? effect_ticks(fx, sw.speed, speedticks[sw.speed]) : speedticks[sw.speed]) *
                                    (MIXMAX - mix) / MIXMAX;
                        if (fx && effect_moves(fx) && sw.fxsel != ovl) autoticks = 1;   /* on at once: it comes in by itself */
                    }
                    if (autoticks < 1) autoticks = 1;
                    Signal(FindTask(NULL), 1UL << sigbit);
                    break;
                } else if (r == ROW_FMCOUNT && i < 3) {     /* S M F */
                    sw.speed = i;
                } else if (r == ROW_TRANS && i == TRANS_BGCOLOR) {  /* BKG colour row on/off */
                    sw.bgpopup = !sw.bgpopup;
                } else if (r == ROW_BGCOLOR && i >= 1) {
                    sw.bgcolor = i;
                    sw.bgpopup = 0;
                    set_labels(&sw, in, &prefs, &fs);
                } else {
                    break;
                }
                inputs_route(in, sw.row[ROW_MAIN].hilite, sw.row[ROW_PRVW].hilite);
                title_monitors(mon, in, &prefs, sw.row[ROW_MAIN].hilite, sw.row[ROW_PRVW].hilite);
                redraw = 1;
                break;
            }
        }
        if (freq && req_file_step(freq, got) && (freqfor != FR_CROUTONS || (!autostart && !mix && seqpos < 0))) {
            char path[512];                     /* it has closed; croutons are not added under a running effect */
            int have = req_file_name(freq, 0, path, sizeof(path));
            if (freqfor == FR_CROUTONS) {
                add_croutons(&sw, &ovl);
            } else if (freqfor == FR_PROJECT) {
                if (have) save_project_to(&sw, path);
            } else if (freqfor == FR_REXX) {
                rxedit_filereq_done(have ? path : NULL);
            } else if (have) {
                still_file(&fs, freqslot, freqfor == FR_STILL_SAVE, path);
                sw.translit = (sw.translit & ~(0xfu << TRANS_FS1)) | 1u << (TRANS_FS1 + fs.cur);
                set_labels(&sw, in, &prefs, &fs);
            }
            req_file_close(freq);
            freq = NULL;
            redraw = 1;
        }
        {
            const char *title, *dir, *file;     /* the one a script asked for, when no other is up */
            if (!freq && rxedit_filereq_wanted(&title, &dir, &file) &&
                !ask_file(w, FR_REXX, 0, title, dir, file, REQF_CENTRE))
                rxedit_filereq_done(NULL);
        }
        if (sw.fxpicks != picks) {
            picks = sw.fxpicks;
            if (FXCUR(&sw) && FXCUR(&sw)->type == FX_FRAM && FXCUR(&sw)->page) {
                frame_page(&sw, &fs, FXCUR(&sw));
                framestore_lights(&sw, &fs);
                set_labels(&sw, in, &prefs, &fs);
                inputs_route(in, sw.row[ROW_MAIN].hilite, sw.row[ROW_PRVW].hilite);
                title_monitors(mon, in, &prefs, sw.row[ROW_MAIN].hilite, sw.row[ROW_PRVW].hilite);
                redraw = 1;
                Signal(FindTask(NULL), 1UL << sigbit);
            }
        }
        if (redraw) {
            sw_render(&sw);
            show(w, &sw, scale, sbuf);
        }
        if (rxewaits == 3 && !rec_active()) {       /* RECORDADD: the clip is written */
            snprintf(rxe.text, sizeof(rxe.text), "%d", rec_status());
            rxe.result = 1;
            rxewaits = 1;
        }
        if (rxewaits && rxewaits != 3 && seqpos < 0 && !autostart) {
            rexx_reply(&rxecall, rxe.rc, rxe.rc2, rxe.result ? rxe.text : NULL);
            rxewaits = 0;
            Signal(FindTask(NULL), 1UL << sigbit);
        }
        if (rxfrom && cgwait) {         /* the CG's port says when it is loaded and when it has gone */
            if (cg_loaded() == (cgwait == 1)) cgwait = 0;
            else if (ticks_now() >= cguntil) {
                rxo.rc = 10;
                cgwait = 0;
            }
        }
        if (rxfrom && !cgwait && !tbarupdate_pending && !autostart && ticks_now() >= rxo.until) {
            rexx_reply(&rxcall, rxo.rc, rxo.rc2, rxo.text);
            rxfrom = NULL;
            Signal(FindTask(NULL), 1UL << sigbit);      /* on to the next call */
        }
    }
    rc = RETURN_OK;
close:
    req_file_close(freq);
    if (rxfrom) rexx_reply(&rxcall, rxo.rc, rxo.rc2, rxo.text);
    rexx_close(rxsw);
    rec_stop();
    out_exit();
    if (rxewaits == 3) {
        snprintf(rxe.text, sizeof(rxe.text), "%d", rec_status());
        rxe.result = 1;
    }
    if (rxewaits) rexx_reply(&rxecall, rxe.rc, rxe.rc2, rxe.result ? rxe.text : NULL);
    rexx_close(rxed);
    req_notice_close();
    if (tpending) {
        AbortIO(&treq->tr_node);
        WaitIO(&treq->tr_node);
    }
    if (treq) {
        CloseDevice(&treq->tr_node);
        DeleteIORequest(&treq->tr_node);
    }
    if (tport) DeleteMsgPort(tport);
    inputswin_close(iw);
    audio_stop(ao);
    if (in) inputs_stop(in);
    plugins_exit();
    if (mon) CloseWindow(mon);
    if (w) CloseWindow(w);
    if (sigbit >= 0) FreeSignal(sigbit);
    free(sbuf);
    free(mainbuf);
    free(prvwbuf);
    free(progbuf);
    free(keybuf);
    free(dvbuf);
    fs_free(&fs);
    for (s = 0; s < sw.nfx; s++) {
        effect_close(&sw.fx[s]);
        free((void *)sw.fx[s].file);
    }
    free(sw.fx);
    picture_free(&sw.bar.sheet);
    if (sw.bar.have_font) colorfont_free(&sw.bar.font);
    sw_close(&sw);
out:
    if (rda) FreeArgs(rda);
    return rc;
}
