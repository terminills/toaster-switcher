/* The software Toaster card on AROS: each opened backend context (a video driver, a file) has a worker thread that
 * grabs the sources of it on the Main, Preview and Key buses, so a camera that takes a frame interval per grab does
 * not hold up a file; the interface copies the latest frames out under a semaphore. A context is used by one thread
 * at a time (its semaphore), and opening, listing and changing sources go through requests to the capture thread.
 * All are pthreads, so backends get a C library context (FFmpeg's file reading and allocation). */
#include <exec/semaphores.h>
#include <exec/memory.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <pthread.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include "inputs.h"
#include "tsinput.h"
#include "plugins.h"

static const struct tsi_backend *const backends[] = {
    &tsi_vhi,
#ifdef HAVE_FFMPEG
    &tsi_file,
#endif
};
#define NBACKENDS (int)(sizeof(backends) / sizeof(backends[0]))
#define MAXOPEN 8

struct slot {
    int open;                   /* index into opened[], -1 = black */
    int source;
    char spec[SPECLEN];
    char name[NAMELEN];
};

enum { REQ_NONE, REQ_SET, REQ_SOURCES, REQ_GRAB, REQ_RELEASE };

enum { BUS_MAIN, BUS_PRVW, BUS_KEY, NBUS };
#define ARING (TSI_AUDIO_RATE / 2)      /* audio per Main/Preview bus: half a second of stereo frames */

struct aring {
    int16_t buf[ARING * 2];
    int r, n;                   /* first frame, frames held */
};

struct inputs {
    struct SignalSemaphore lock;
    struct Task *ui;
    ULONG uisig;
    pthread_t thread;
    int started;
    volatile int ready;         /* the capture thread has been through every slot once */
    volatile int quit;
    int w, h;
    struct slot slot[NSLOTS];
    struct opened {
        const struct tsi_backend *be;   /* NULL: a place that is free */
        void *ctx;
        char arg[SPECLEN];
        struct SignalSemaphore use;     /* held around every call on ctx: one thread at a time */
        struct inputs *in;
        pthread_t worker;               /* grabs the buses whose source is in this ctx */
        int running;
        volatile int stop;              /* the worker is to end: the context is being closed */
        volatile uint32_t calls, frames, usecs;    /* grab calls, frames delivered, time in grab (for STATS) */
        volatile uint32_t loops, lockus, delayus;  /* worker loops, time waiting for in->lock and in Delay */
        uint32_t *back;                 /* the worker's grab buffer */
        int16_t abuf[4096 * 2];         /* the worker's sound drain */
    } opened[MAXOPEN];
    int nopened;
    int route[NBUS];            /* Main, Preview, Key slots */
    uint32_t seq;
    uint32_t *front[NBUS];      /* latest frame per bus */
    int isblack[NBUS];          /* front[] holds the black of a bus without a source */
    struct aring *ring[2];      /* sound of the Main and Preview buses (under lock) */
    struct aring *sring[NSLOTS];    /* sound of SOUND_ALWAYS slots on neither bus */
    int sound[NSLOTS];          /* SOUND_FOLLOW, SOUND_ALWAYS, SOUND_OFF */
    /* one request at a time from the interface */
    volatile int req, reqdone, reqresult;
    int reqslot, reqmax;
    char reqspec[SPECLEN];
    char (*reqnames)[NAMELEN];
    uint32_t *reqbuf;
};


/* No C library on the capture process: small helpers instead of sprintf/atoi. */
static void putnum(char *d, int n)
{
    char t[12];
    int i = 0;
    do t[i++] = '0' + n % 10; while ((n /= 10) && i < 11);
    while (i) *d++ = t[--i];
    *d = 0;
}

static void copy(char *d, const char *s, int len)
{
    int n = strlen(s);
    if (n > len - 1) n = len - 1;
    memcpy(d, s, n);
    d[n] = 0;
}

static void *worker(void *arg);

/* The context of backend name with arg, opened when it is not yet: its place in opened[], -1 when it does not
 * open. A backend is one linked in, or else a plug-in's, which is held until the context is closed. */
static int open_backend(struct inputs *in, const char *name, const char *arg)
{
    const struct tsi_backend *be = NULL;
    struct opened *o;
    int b, i, at = -1;
    for (i = 0; i < in->nopened; i++) {
        if (!in->opened[i].be) {
            if (at < 0) at = i;
        } else if (!strcmp(in->opened[i].be->name, name) && !strcmp(in->opened[i].arg, arg)) {
            return in->opened[i].ctx ? i : -1;
        }
    }
    if (at < 0 && in->nopened == MAXOPEN) return -1;
    for (b = 0; b < NBACKENDS && !be; b++)
        if (!strcmp(backends[b]->name, name) && backends[b]->api_version == TSI_API_VERSION) be = backends[b];
    if (!be && !(be = plugins_hold_input(name))) return -1;
    if (at < 0) at = in->nopened++;
    o = &in->opened[at];
    memset(o, 0, sizeof(*o));
    o->be = be;
    copy(o->arg, arg, sizeof(o->arg));
    o->ctx = be->open(arg);
    InitSemaphore(&o->use);
    o->in = in;
    if (o->ctx && (o->back = AllocVec((size_t)in->w * in->h * 4, MEMF_ANY)))
        o->running = !pthread_create(&o->worker, NULL, worker, o);
    return o->ctx ? at : -1;
}

/* End a context's worker and close it; its place is free then. */
static void close_opened(struct opened *o)
{
    if (!o->be) return;
    o->stop = 1;
    if (o->running) pthread_join(o->worker, NULL);
    if (o->ctx) o->be->close(o->ctx);
    if (o->back) FreeVec(o->back);
    plugins_release_input(o->be);
    o->running = 0;
    o->ctx = NULL;
    o->back = NULL;
    o->be = NULL;
}

/* Close the plug-ins' contexts no input shows a source of: a plug-in is free to go when none of its are open. */
static int release_unused(struct inputs *in)
{
    int i, s, b, n = 0;
    for (i = 0; i < in->nopened; i++) {
        for (s = 0; s < NSLOTS && in->slot[s].open != i; s++)
            ;
        for (b = 0; b < NBACKENDS && in->opened[i].be != backends[b]; b++)
            ;
        if (s == NSLOTS && b == NBACKENDS && in->opened[i].be) {
            close_opened(&in->opened[i]);
            n++;
        }
    }
    return n;
}

/* "backend:arg:source" -> backend, arg, source; 0 on success */
static int parse(const char *spec, char *name, int namelen, char *arg, int arglen, int *source)
{
    const char *p = strchr(spec, ':'), *q = p ? strrchr(spec, ':') : NULL;
    int n = 0;
    if (!p || q == p || p - spec >= namelen || q - p - 1 >= arglen || !q[1]) return -1;
    memcpy(name, spec, p - spec); name[p - spec] = 0;
    memcpy(arg, p + 1, q - p - 1); arg[q - p - 1] = 0;
    for (q++; *q; q++) {
        if (*q < '0' || *q > '9') return -1;
        n = n * 10 + (*q - '0');
    }
    *source = n;
    return 0;
}

static int assign(struct inputs *in, int s, const char *spec)
{
    char name[16], arg[SPECLEN], nm[NAMELEN];
    struct slot sl;
    int ok = -1;
    sl.open = -1;
    sl.source = 0;
    copy(sl.spec, spec, sizeof(sl.spec));
    copy(sl.name, "black", sizeof(sl.name));
    if (spec[0] && !parse(spec, name, sizeof(name), arg, sizeof(arg), &sl.source) &&
        (sl.open = open_backend(in, name, arg)) >= 0) {
        struct opened *o = &in->opened[sl.open];
        int n, named;
        ObtainSemaphore(&o->use);
        n = o->be->count(o->ctx);
        named = sl.source < n && !o->be->source_name(o->ctx, sl.source, nm, sizeof(nm));
        ReleaseSemaphore(&o->use);
        if (sl.source < n) {
            if (!named) copy(nm, "?", sizeof(nm));
            copy(sl.name, nm, sizeof(sl.name));
            ok = 0;
        } else {
            sl.open = -1;
            copy(sl.name, "missing", sizeof(sl.name));
        }
    } else if (spec[0]) {
        copy(sl.name, "missing", sizeof(sl.name));
    } else {
        ok = 0;
    }
    ObtainSemaphore(&in->lock);
    in->slot[s] = sl;
    ReleaseSemaphore(&in->lock);
    return ok;
}

/* Inputs 1-4 without a spec take the UVC devices in order. DV1/DV2 (framestores) and BKG (matte) stay black. */
static void assign_auto(struct inputs *in, const int *isauto)
{
    int s, next = 0, count, o = open_backend(in, "vhi", "uvc");
    char spec[SPECLEN];
    count = 0;
    if (o >= 0) {
        ObtainSemaphore(&in->opened[o].use);
        count = in->opened[o].be->count(in->opened[o].ctx);
        ReleaseSemaphore(&in->opened[o].use);
    }
    for (s = SLOT_IN1; s <= SLOT_IN4; s++) {
        int src;
        if (!isauto[s]) continue;
        if (next < count) src = next++;
        else { assign(in, s, ""); continue; }
        strcpy(spec, "vhi:uvc:");
        putnum(spec + strlen(spec), src);
        assign(in, s, spec);
    }
    for (s = SLOT_DV1; s < NSLOTS; s++)
        if (isauto[s]) assign(in, s, "");
}

static void serve_request(struct inputs *in)
{
    char name[16], arg[SPECLEN];
    int r = -1, i, n, src;
    if (in->req == REQ_SET) {
        r = assign(in, in->reqslot, in->reqspec);
    } else if (in->req == REQ_SOURCES) {
        /* reqspec holds "backend:arg:0" */
        if (!parse(in->reqspec, name, sizeof(name), arg, sizeof(arg), &src) && (i = open_backend(in, name, arg)) >= 0) {
            ObtainSemaphore(&in->opened[i].use);
            n = in->opened[i].be->count(in->opened[i].ctx);
            for (r = 0; r < n && r < in->reqmax; r++)
                if (in->opened[i].be->source_name(in->opened[i].ctx, r, in->reqnames[r], NAMELEN))
                    copy(in->reqnames[r], "?", NAMELEN);
            ReleaseSemaphore(&in->opened[i].use);
        }
    } else if (in->req == REQ_RELEASE) {
        r = release_unused(in);
    } else if (in->req == REQ_GRAB) {      /* a frame of a slot on no bus: the source's next one, within 1 s */
        struct slot *sl = &in->slot[in->reqslot];
        if (sl->open < 0) {
            memset(in->reqbuf, 0, (size_t)in->w * in->h * 4);
            r = 0;
        } else {
            for (i = 0; i < 50 && r; i++) {
                struct opened *o = &in->opened[sl->open];
                ObtainSemaphore(&o->use);
                r = o->be->grab(o->ctx, sl->source, in->reqbuf, in->w, in->h);
                ReleaseSemaphore(&o->use);
                if (r) Delay(1);
            }
            r = r ? -1 : 0;
        }
    }
    in->reqresult = r;
    in->req = REQ_NONE;
    in->reqdone = 1;
    Signal(in->ui, in->uisig);
}

/* Ring r takes n frames (the oldest go when it is full). Under in->lock. */
static void ring_put(struct aring *a, const int16_t *f, int n)
{
    int i;
    if (n > ARING) { f += (n - ARING) * 2; n = ARING; }
    if (a->n + n > ARING) {
        int drop = a->n + n - ARING;
        a->r = (a->r + drop) % ARING;
        a->n -= drop;
    }
    for (i = 0; i < n; i++) {
        int w = (a->r + a->n + i) % ARING;
        a->buf[w * 2] = f[i * 2];
        a->buf[w * 2 + 1] = f[i * 2 + 1];
    }
    a->n += n;
}

/* A context's worker: the buses showing its sources, and its SOUND_ALWAYS sources on neither bus for their sound. */
static void *worker(void *arg)
{
    struct opened *o = arg;
    struct inputs *in = o->in;
    int me = o - in->opened, bus, s;
    if (o->be->priority) SetTaskPri(FindTask(NULL), o->be->priority);
    while (!in->quit && !o->stop) {
        int grabbed = 0;
        for (bus = 0; bus < NBUS && !in->quit && !o->stop; bus++) {
            struct slot sl;
            int r, rc, na = 0;
            struct timeval l0, l1;
            gettimeofday(&l0, NULL);
            ObtainSemaphoreShared(&in->lock);
            gettimeofday(&l1, NULL);
            o->lockus += (l1.tv_sec - l0.tv_sec) * 1000000 + (l1.tv_usec - l0.tv_usec);
            r = in->route[bus];
            if (bus == BUS_KEY && (r == in->route[BUS_MAIN] || r == in->route[BUS_PRVW])) r = -1;  /* shared */
            sl.open = r >= 0 && r < NSLOTS ? in->slot[r].open : -1;
            sl.source = r >= 0 && r < NSLOTS ? in->slot[r].source : 0;
            ReleaseSemaphore(&in->lock);
            if (sl.open != me) continue;
            ObtainSemaphore(&o->use);
            {
                struct timeval t0, t1;
                gettimeofday(&t0, NULL);
                rc = o->be->grab(o->ctx, sl.source, o->back, in->w, in->h);
                gettimeofday(&t1, NULL);
                o->calls++;
                o->usecs += (t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_usec - t0.tv_usec);
            }
            if (rc) {
                ReleaseSemaphore(&o->use);
                continue;
            }
            o->frames++;
            ObtainSemaphore(&in->lock);
            if (in->route[bus] == r && in->slot[r].open == sl.open && in->slot[r].source == sl.source) {
                uint32_t *t = in->front[bus];
                in->front[bus] = o->back;
                o->back = t;
                in->isblack[bus] = 0;
                in->seq++;
                grabbed = 1;
            }
            ReleaseSemaphore(&in->lock);
            /* all the sound that came with it (a late wake brings several frames' worth) */
            while (bus != BUS_KEY && o->be->audio && (na = o->be->audio(o->ctx, sl.source, o->abuf, 4096)) > 0) {
                ObtainSemaphore(&in->lock);
                if (in->route[bus] == r) ring_put(in->ring[bus], o->abuf, na);
                ReleaseSemaphore(&in->lock);
                if (na < 4096) break;
            }
            ReleaseSemaphore(&o->use);
        }
        for (s = 0; s < NSLOTS && !in->quit && !o->stop; s++) {     /* sound that plays whatever is on air */
            struct slot sl;
            int on, na = 0;
            ObtainSemaphoreShared(&in->lock);
            sl = in->slot[s];
            on = in->sound[s] == SOUND_ALWAYS && s != in->route[BUS_MAIN] && s != in->route[BUS_PRVW] && sl.open == me;
            ReleaseSemaphore(&in->lock);
            if (!on || !o->be->audio) continue;
            ObtainSemaphore(&o->use);
            if (!o->be->grab(o->ctx, sl.source, o->back, in->w, in->h))
                while ((na = o->be->audio(o->ctx, sl.source, o->abuf, 4096)) > 0) {
                    ObtainSemaphore(&in->lock);
                    if (!in->sring[s]) in->sring[s] = AllocVec(sizeof(struct aring), MEMF_ANY | MEMF_CLEAR);
                    if (in->sring[s]) ring_put(in->sring[s], o->abuf, na);
                    ReleaseSemaphore(&in->lock);
                    if (na < 4096) break;
                }
            ReleaseSemaphore(&o->use);
        }
        if (grabbed) Signal(in->ui, in->uisig);
        {
            struct timeval d0, d1;
            gettimeofday(&d0, NULL);
            Delay(1);
            gettimeofday(&d1, NULL);
            o->delayus += (d1.tv_sec - d0.tv_sec) * 1000000 + (d1.tv_usec - d0.tv_usec);
            o->loops++;
        }
    }
    return NULL;
}

/* The capture thread: the starting assignment, then requests, and black for buses without a source. */
static void *capture(void *arg)
{
    struct inputs *in = arg;
    int s, bus, isauto[NSLOTS];

    for (s = 0; s < NSLOTS; s++) {
        isauto[s] = in->slot[s].open == -2;     /* inputs_start marks automatic slots */
        if (!isauto[s]) assign(in, s, in->slot[s].spec);
    }
    assign_auto(in, isauto);
    in->ready = 1;
    Signal(in->ui, in->uisig);
    while (!in->quit) {
        int blacked = 0;
        if (in->req != REQ_NONE) serve_request(in);
        ObtainSemaphore(&in->lock);
        for (bus = 0; bus < NBUS; bus++) {
            int r = in->route[bus];
            if ((r < 0 || r >= NSLOTS || in->slot[r].open < 0) && !in->isblack[bus]) {
                memset(in->front[bus], 0, (size_t)in->w * in->h * 4);
                in->isblack[bus] = 1;
                in->seq++;
                blacked = 1;
            }
        }
        ReleaseSemaphore(&in->lock);
        if (blacked) Signal(in->ui, in->uisig);
        Delay(1);
    }
    for (s = 0; s < in->nopened; s++)
        in->opened[s].stop = 1;
    for (s = 0; s < in->nopened; s++)
        close_opened(&in->opened[s]);
    return NULL;
}

struct inputs *inputs_start(const char *const specs[NSLOTS], int w, int h, int sigbit)
{
    struct inputs *in = AllocVec(sizeof(*in), MEMF_ANY | MEMF_CLEAR);
    int s, i;
    if (!in) return NULL;
    InitSemaphore(&in->lock);
    in->ui = FindTask(NULL);
    in->uisig = 1UL << sigbit;
    in->w = w; in->h = h;
    in->route[BUS_MAIN] = in->route[BUS_PRVW] = in->route[BUS_KEY] = -1;
    for (s = 0; s < NSLOTS; s++) {
        in->slot[s].open = specs && specs[s] ? -1 : -2;
        if (specs && specs[s]) copy(in->slot[s].spec, specs[s], SPECLEN);
        copy(in->slot[s].name, "black", NAMELEN);
    }
    for (i = 0; i < NBUS; i++)
        in->front[i] = AllocVec((size_t)w * h * 4, MEMF_ANY | MEMF_CLEAR);
    for (i = 0; i < 2; i++) in->ring[i] = AllocVec(sizeof(struct aring), MEMF_ANY | MEMF_CLEAR);
    if (!in->front[0] || !in->front[1] || !in->front[2] || !in->ring[0] || !in->ring[1]) {
        inputs_stop(in);
        return NULL;
    }
    {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 1024 * 1024);
        in->started = !pthread_create(&in->thread, &attr, capture, in);
        pthread_attr_destroy(&attr);
    }
    if (!in->started) { inputs_stop(in); return NULL; }
    return in;
}

void inputs_stop(struct inputs *in)
{
    int i;
    if (!in) return;
    if (in->started) {
        in->quit = 1;
        pthread_join(in->thread, NULL);
    }
    for (i = 0; i < NBUS; i++) if (in->front[i]) FreeVec(in->front[i]);
    for (i = 0; i < 2; i++) if (in->ring[i]) FreeVec(in->ring[i]);
    for (i = 0; i < NSLOTS; i++) if (in->sring[i]) FreeVec(in->sring[i]);
    FreeVec(in);
}

void inputs_route(struct inputs *in, int main_slot, int prvw_slot)
{
    ObtainSemaphore(&in->lock);
    if (main_slot == in->route[1] && prvw_slot == in->route[0] && main_slot != prvw_slot) {
        uint32_t *t = in->front[0];     /* the buses traded places: so do their latest frames and sound */
        struct aring *a = in->ring[0];
        in->front[0] = in->front[1];
        in->front[1] = t;
        int b = in->isblack[0];
        in->ring[0] = in->ring[1];
        in->ring[1] = a;
        in->isblack[0] = in->isblack[1];
        in->isblack[1] = b;
    } else {
        if (main_slot != in->route[0]) in->ring[0]->n = 0, in->isblack[0] = 0;
        if (prvw_slot != in->route[1]) in->ring[1]->n = 0, in->isblack[1] = 0;
    }
    in->route[0] = main_slot;
    in->route[1] = prvw_slot;
    ReleaseSemaphore(&in->lock);
}

/* Take n frames from ring a (silence where it runs short) into t. */
static void ring_take(struct aring *a, int16_t *t, int n)
{
    int i, k = a && n < a->n ? n : a ? a->n : 0;
    for (i = 0; i < k; i++) {
        int j = (a->r + i) % ARING;
        t[i * 2] = a->buf[j * 2];
        t[i * 2 + 1] = a->buf[j * 2 + 1];
    }
    memset(t + k * 2, 0, (size_t)(n - k) * 4);
    if (a) {
        a->r = (a->r + k) % ARING;
        a->n -= k;
    }
}

void inputs_program_audio(struct inputs *in, int16_t *dst, int frames, int mix, int mixmax)
{
    static int16_t t[4096 * 2];
    static int32_t acc[4096 * 2];
    int b, s, i;
    if (frames > 4096) frames = 4096;
    memset(acc, 0, (size_t)frames * 8);
    ObtainSemaphore(&in->lock);
    for (b = 0; b < 2; b++) {           /* Main fades out as Preview fades in, unless their slot says otherwise */
        int slot = in->route[b], mode = slot >= 0 && slot < NSLOTS ? in->sound[slot] : SOUND_OFF;
        int g = mode == SOUND_ALWAYS ? mixmax : b == 0 ? mixmax - mix : mix;
        ring_take(in->ring[b], t, frames);
        if (mode == SOUND_OFF || (b == 1 && slot == in->route[0])) continue;
        for (i = 0; i < frames * 2; i++) acc[i] += t[i] * g / mixmax;
    }
    for (s = 0; s < NSLOTS; s++)
        if (in->sring[s]) {
            int on = in->sound[s] == SOUND_ALWAYS && s != in->route[0] && s != in->route[1];
            ring_take(in->sring[s], t, frames);
            if (on)
                for (i = 0; i < frames * 2; i++) acc[i] += t[i];
        }
    ReleaseSemaphore(&in->lock);
    for (i = 0; i < frames * 2; i++) dst[i] = acc[i] > 32767 ? 32767 : acc[i] < -32768 ? -32768 : acc[i];
}

void inputs_sound(struct inputs *in, int slot, int mode)
{
    if (slot < 0 || slot >= NSLOTS) return;
    ObtainSemaphore(&in->lock);
    in->sound[slot] = mode;
    ReleaseSemaphore(&in->lock);
}

void inputs_route_key(struct inputs *in, int key_slot)
{
    ObtainSemaphore(&in->lock);
    if (in->route[BUS_KEY] != key_slot) in->isblack[BUS_KEY] = 0;
    in->route[BUS_KEY] = key_slot;
    ReleaseSemaphore(&in->lock);
}

void inputs_key(struct inputs *in, uint32_t *keybuf)
{
    int k;
    ObtainSemaphoreShared(&in->lock);
    k = in->route[BUS_KEY] == in->route[BUS_MAIN] ? BUS_MAIN : in->route[BUS_KEY] == in->route[BUS_PRVW] ? BUS_PRVW : BUS_KEY;
    memcpy(keybuf, in->front[k], (size_t)in->w * in->h * 4);
    ReleaseSemaphore(&in->lock);
}

uint32_t inputs_frames(struct inputs *in, uint32_t *mainbuf, uint32_t *prvwbuf)
{
    uint32_t seq;
    ObtainSemaphoreShared(&in->lock);
    if (mainbuf) memcpy(mainbuf, in->front[0], (size_t)in->w * in->h * 4);
    if (prvwbuf) memcpy(prvwbuf, in->front[1], (size_t)in->w * in->h * 4);
    seq = in->seq;
    ReleaseSemaphore(&in->lock);
    return seq;
}

const char *inputs_backend(int index)
{
    return index >= 0 && index < NBACKENDS ? backends[index]->name : NULL;
}

const char *inputs_slot_name(struct inputs *in, int slot)
{
    return slot >= 0 && slot < NSLOTS ? in->slot[slot].name : "";
}

const char *inputs_slot_spec(struct inputs *in, int slot)
{
    return slot >= 0 && slot < NSLOTS ? in->slot[slot].spec : "";
}

int inputs_ready(struct inputs *in)
{
    return in->ready;
}

int inputs_slot_missing(struct inputs *in, int slot)
{
    return slot >= 0 && slot < NSLOTS && in->slot[slot].spec[0] && in->slot[slot].open < 0;
}

/* Hand a request to the capture thread and wait for it; frame signals meanwhile are left pending again. */
static int request(struct inputs *in)
{
    ULONG extra = 0;
    in->reqdone = 0;
    in->req = in->req;     /* set by the caller */
    while (!in->reqdone && !in->quit)
        extra |= Wait(in->uisig);
    if (extra) SetSignal(in->uisig, in->uisig);
    return in->reqdone ? in->reqresult : -1;
}

int inputs_grab(struct inputs *in, int slot, uint32_t *buf)
{
    int b, found = -1;
    if (slot < 0 || slot >= NSLOTS) return -1;
    ObtainSemaphoreShared(&in->lock);
    for (b = 0; b < NBUS && found < 0; b++)
        if (in->route[b] == slot) found = b;
    if (found >= 0) memcpy(buf, in->front[found], (size_t)in->w * in->h * 4);
    ReleaseSemaphore(&in->lock);
    if (found >= 0) return 0;
    in->reqslot = slot;
    in->reqbuf = buf;
    in->req = REQ_GRAB;
    return request(in);
}

int inputs_set(struct inputs *in, int slot, const char *spec)
{
    if (slot < 0 || slot >= NSLOTS) return -1;
    copy(in->reqspec, spec, SPECLEN);
    in->reqslot = slot;
    in->req = REQ_SET;
    return request(in);
}

int inputs_sources(struct inputs *in, const char *backend, const char *arg, char names[][NAMELEN], int max)
{
    if (strlen(backend) + strlen(arg) + 4 > SPECLEN) return -1;
    strcpy(in->reqspec, backend);
    strcat(in->reqspec, ":");
    strcat(in->reqspec, arg);
    strcat(in->reqspec, ":0");
    in->reqnames = names;
    in->reqmax = max;
    in->req = REQ_SOURCES;
    return request(in);
}

int inputs_release_plugins(struct inputs *in)
{
    in->req = REQ_RELEASE;
    return request(in);
}

int inputs_stats(struct inputs *in, char *buf, int len)
{
    int i, n = 0;
    buf[0] = 0;
    for (i = 0; i < in->nopened && n < len - 80; i++) {
        struct opened *o = &in->opened[i];
        const struct tsi_backend *be = o->be;
        const char *a = strrchr(o->arg, '/');
        uint32_t c = o->calls, f = o->frames, u = o->usecs, lp = o->loops, lk = o->lockus, dl = o->delayus;
        o->calls = o->frames = o->usecs = o->loops = o->lockus = o->delayus = 0;
        if (!c || !be) continue;
        n += snprintf(buf + n, len - n, "  %s:%.24s loops %lu calls %lu frames %lu grab-ms %lu lock-ms %lu delay-ms %lu\n",
                      be->name, a ? a + 1 : o->arg, (unsigned long)lp, (unsigned long)c, (unsigned long)f,
                      (unsigned long)(u / 1000), (unsigned long)(lk / 1000), (unsigned long)(dl / 1000));
    }
    return n;
}
