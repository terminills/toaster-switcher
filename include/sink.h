/* One place Program goes to through a tsoutput.h backend: a thread that opens the backend, hands it the newest
 * picture 30 times a second by the clock with the sound made since, and closes it when it is stopped, has written
 * what it was to, or the backend fails. The clip recorder is one (record.c), each output that is on another
 * (outputs.c). */
#ifndef SINK_H
#define SINK_H
#include <pthread.h>
#include <stdint.h>
#include "tsoutput.h"

#define SINK_FPS 30

enum {
    SINK_IDLE,                  /* not started yet */
    SINK_OPENING, SINK_RUNNING,
    SINK_ENDED,                 /* closed, all written */
    SINK_FAILED,                /* the backend failed while it ran, or at its close */
    SINK_NOTOPEN                /* the backend did not open */
};

struct sink {
    pthread_mutex_t lock;
    pthread_t thread;
    int ready, started;         /* set up; there is a thread to join */
    volatile int state, quit, paused;
    const struct tso_backend *be;
    char arg[256];
    int w, h, havepic;
    long limit;                 /* pictures to write, 0: until stopped */
    uint32_t *pic;              /* the newest picture */
    int16_t *ring;
    int rhead, rcount;          /* frames of sound in ring, the oldest at rhead */
};

/* Set s up for pictures of w x h; 0 when it can be used. Once. */
int sink_init(struct sink *s, int w, int h);
/* Start with be's argument, for limit pictures or with 0 until sink_stop. With wait it returns once the backend has
 * opened or has not. 0 when it runs (without wait: when it opens), else its state is SINK_NOTOPEN. */
int sink_start(struct sink *s, const struct tso_backend *be, const char *arg, long limit, int wait);
/* End it, if it runs, and wait for the backend's close. */
void sink_stop(struct sink *s);
void sink_pause(struct sink *s, int on);
int sink_active(const struct sink *s);
/* Program as it was just composed, and its sound as it is played; from any thread. */
void sink_video(struct sink *s, const uint32_t *rgb);
void sink_audio(struct sink *s, const int16_t *pcm, int frames);
#endif
