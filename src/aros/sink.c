/* A place Program goes to (see sink.h): its thread writes the newest picture it was given, 30 times a second by the
 * clock, and the sound given since, as much as goes with the pictures so far. The backend is opened, called and
 * closed by that thread alone. */
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include "sink.h"

#define RING TSO_AUDIO_RATE     /* a second of sound waits for the thread at most */

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

int sink_init(struct sink *s, int w, int h)
{
    if (s->ready) return 0;
    if (pthread_mutex_init(&s->lock, NULL)) return -1;
    s->w = w;
    s->h = h;
    s->pic = malloc((size_t)w * h * 4);
    s->ring = malloc(RING * 4);
    if (!s->pic || !s->ring) {
        free(s->pic);
        free(s->ring);
        s->pic = NULL;
        s->ring = NULL;
        return -1;
    }
    s->ready = 1;
    return 0;
}

int sink_active(const struct sink *s)
{
    return s->state == SINK_OPENING || s->state == SINK_RUNNING;
}

void sink_video(struct sink *s, const uint32_t *rgb)
{
    if (!s->ready || !sink_active(s)) return;
    pthread_mutex_lock(&s->lock);
    memcpy(s->pic, rgb, (size_t)s->w * s->h * 4);
    s->havepic = 1;
    pthread_mutex_unlock(&s->lock);
}

void sink_audio(struct sink *s, const int16_t *pcm, int frames)
{
    int i;
    if (!s->ready || !sink_active(s) || s->paused) return;
    pthread_mutex_lock(&s->lock);
    for (i = 0; i < frames && s->rcount < RING; i++, s->rcount++)
        memcpy(&s->ring[((s->rhead + s->rcount) % RING) * 2], &pcm[i * 2], 4);
    pthread_mutex_unlock(&s->lock);
}

static void *run(void *arg)
{
    struct sink *s = arg;
    uint32_t *out = malloc((size_t)s->w * s->h * 4);
    int16_t *snd = malloc(RING * 4);
    void *ctx = NULL;
    long n = 0, sound = 0;      /* pictures and frames of sound written */
    double t0 = 0;
    int bad = 0, have = 0;
    /* a volume that is not there fails, it is not asked for */
    ((struct Process *)FindTask(NULL))->pr_WindowPtr = (APTR)-1;
    if (out && snd) ctx = s->be->open(s->arg, s->w, s->h, SINK_FPS);
    if (!ctx) {
        free(out);
        free(snd);
        s->state = SINK_NOTOPEN;
        return NULL;
    }
    s->state = SINK_RUNNING;
    while (!s->quit && !bad && (!s->limit || n < s->limit)) {
        double wait;
        int ns = 0;
        if (!have || s->paused) {               /* the clock starts with the first picture, and again after a pause */
            usleep(1000000 / SINK_FPS / 2);
            pthread_mutex_lock(&s->lock);
            have = s->havepic;
            if (s->paused) s->rcount = 0;
            pthread_mutex_unlock(&s->lock);
            t0 = now() - (double)n / SINK_FPS;
            continue;
        }
        wait = t0 + (double)n / SINK_FPS - now();
        if (wait < -1.0) t0 = now() - (double)n / SINK_FPS;     /* far behind (a long stall): carry on from here */
        if (wait > 0) usleep((useconds_t)(wait * 1e6));
        pthread_mutex_lock(&s->lock);
        memcpy(out, s->pic, (size_t)s->w * s->h * 4);
        while (s->rcount && sound + ns < (n + 1) * (long)(TSO_AUDIO_RATE / SINK_FPS)) {
            memcpy(&snd[ns++ * 2], &s->ring[s->rhead * 2], 4);
            s->rhead = (s->rhead + 1) % RING;
            s->rcount--;
        }
        pthread_mutex_unlock(&s->lock);
        bad = s->be->video(ctx, out) || (ns && s->be->audio(ctx, snd, ns));
        sound += ns;
        n++;
    }
    if (s->be->close(ctx)) bad = 1;
    free(out);
    free(snd);
    s->state = bad ? SINK_FAILED : SINK_ENDED;
    return NULL;
}

int sink_start(struct sink *s, const struct tso_backend *be, const char *arg, long limit, int wait)
{
    if (!s->ready || sink_active(s) || strlen(arg) >= sizeof(s->arg)) return -1;
    sink_stop(s);               /* the one before ended by itself */
    s->be = be;
    strcpy(s->arg, arg);
    s->limit = limit;
    s->havepic = s->rhead = s->rcount = 0;
    s->quit = s->paused = 0;
    s->state = SINK_OPENING;
    if (pthread_create(&s->thread, NULL, run, s)) {
        s->state = SINK_NOTOPEN;
        return -1;
    }
    s->started = 1;
    while (wait && s->state == SINK_OPENING)
        usleep(2000);
    return s->state == SINK_NOTOPEN ? -1 : 0;
}

void sink_stop(struct sink *s)
{
    if (!s->started) return;
    s->quit = 1;
    pthread_join(s->thread, NULL);
    s->started = 0;
}

void sink_pause(struct sink *s, int on)
{
    s->paused = on;
}
