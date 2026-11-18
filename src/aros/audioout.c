/* Program sound through ahi.device (see audioout.h): a thread keeps NREQ CMD_WRITE requests queued, each a block of
 * Program's sound (inputs_program_audio: Main and Preview mixed by the T-bar's position), and reports its peaks.
 * A recording and the outputs get every block too. */
#include <exec/memory.h>
#include <devices/ahi.h>
#include <proto/exec.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include "inputs.h"
#include "tsinput.h"
#include "audioout.h"
#include "record.h"
#include "outputs.h"

#define BLOCK 4096              /* frames per request: 85 ms */
#define NREQ 2                  /* requests queued: 170 ms to ride out a stall; AHI links no more than two */
#define PACED 12                /* blocks after which the device is asked whether it took their time */

struct audioout {
    struct inputs *in;
    volatile int *mix, *levels;
    int mixmax;
    volatile int quit;
    pthread_t thread;
};

static void fill(struct audioout *ao, int16_t *out)
{
    int m = *ao->mix, i, pl = 0, pr = 0;
    if (m < 0) m = 0;
    if (m > ao->mixmax) m = ao->mixmax;
    inputs_program_audio(ao->in, out, BLOCK, m, ao->mixmax);
    rec_audio(out, BLOCK);
    out_audio(out, BLOCK);
    for (i = 0; i < BLOCK; i++) {
        int l = out[i * 2] < 0 ? -out[i * 2] : out[i * 2], r = out[i * 2 + 1] < 0 ? -out[i * 2 + 1] : out[i * 2 + 1];
        if (l > pl) pl = l;
        if (r > pr) pr = r;
    }
    if (ao->levels) {
        ao->levels[0] = pl;
        ao->levels[1] = pr;
    }
}

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

/* Without a device that plays the blocks are still made, at the pace they would play: for the meters and a recording. */
static void unheard(struct audioout *ao)
{
    int16_t *buf = AllocVec(BLOCK * 4, MEMF_ANY | MEMF_CLEAR);
    double t0 = now(), wait;
    long n = 0;
    while (buf && !ao->quit) {
        fill(ao, buf);
        wait = t0 + (double)++n * BLOCK / TSI_AUDIO_RATE - now();
        if (wait < -1.0) {                      /* far behind (a long stall): carry on from here */
            t0 = now();
            n = 0;
        }
        if (wait > 0) usleep((useconds_t)(wait * 1e6));
    }
    FreeVec(buf);
}

static void *play(void *arg)
{
    struct audioout *ao = arg;
    struct MsgPort *port = CreateMsgPort();
    struct AHIRequest *req[NREQ] = { NULL };
    int16_t *buf[NREQ] = { NULL };
    int k, sent[NREQ] = { 0 }, prev = -1, n = 0, mute = 0;
    double t0;
    SetTaskPri(FindTask(NULL), 2);      /* run before the interface's drawing */
    if (!port || !(req[0] = (struct AHIRequest *)CreateIORequest(port, sizeof(struct AHIRequest)))) goto done;
    req[0]->ahir_Version = 4;
    if (OpenDevice((CONST_STRPTR)AHINAME, AHI_DEFAULT_UNIT, (struct IORequest *)req[0], 0)) {
        DeleteIORequest((struct IORequest *)req[0]);
        req[0] = NULL;
        mute = 1;
        goto done;
    }
    for (k = 1; k < NREQ; k++) {
        if (!(req[k] = AllocVec(sizeof(struct AHIRequest), MEMF_ANY))) goto close;
        memcpy(req[k], req[0], sizeof(struct AHIRequest));
    }
    for (k = 0; k < NREQ; k++)
        if (!(buf[k] = AllocVec(BLOCK * 4, MEMF_ANY | MEMF_CLEAR))) goto close;
    t0 = now();
    for (k = 0; !ao->quit; k = (k + 1) % NREQ) {
        struct AHIRequest *r = req[k];
        if (sent[k]) WaitIO((struct IORequest *)r);     /* the oldest has played */
        if (++n == PACED) {
            /* a unit without an audio mode takes the blocks and plays none: it would be fed as fast as they are made */
            if (now() - t0 < PACED * BLOCK / (2.0 * TSI_AUDIO_RATE)) {
                mute = 1;
                break;
            }
            n = 0;
            t0 = now();
        }
        fill(ao, buf[k]);
        r->ahir_Std.io_Command = CMD_WRITE;
        r->ahir_Std.io_Data = buf[k];
        r->ahir_Std.io_Length = BLOCK * 4;
        r->ahir_Std.io_Offset = 0;
        r->ahir_Frequency = TSI_AUDIO_RATE;
        r->ahir_Type = AHIST_S16S;
        r->ahir_Volume = 0x10000;
        r->ahir_Position = 0x8000;
        r->ahir_Link = prev >= 0 && sent[prev] ? req[prev] : NULL;  /* played right after the one before */
        SendIO((struct IORequest *)r);
        sent[k] = 1;
        prev = k;
    }
    for (k = 0; k < NREQ; k++)
        if (sent[k]) {
            AbortIO((struct IORequest *)req[k]);
            WaitIO((struct IORequest *)req[k]);
        }
close:
    CloseDevice((struct IORequest *)req[0]);
    DeleteIORequest((struct IORequest *)req[0]);
    for (k = 1; k < NREQ; k++) FreeVec(req[k]);
    for (k = 0; k < NREQ; k++) FreeVec(buf[k]);
done:
    if (port) DeleteMsgPort(port);
    if (mute) unheard(ao);
    return NULL;
}

struct audioout *audio_start(struct inputs *in, volatile int *mix, int mixmax, volatile int *levels)
{
    struct audioout *ao = calloc(1, sizeof(*ao));
    if (!ao) return NULL;
    ao->in = in;
    ao->mix = mix;
    ao->mixmax = mixmax;
    ao->levels = levels;
    if (pthread_create(&ao->thread, NULL, play, ao)) {
        free(ao);
        return NULL;
    }
    return ao;
}

void audio_stop(struct audioout *ao)
{
    if (!ao) return;
    ao->quit = 1;
    pthread_join(ao->thread, NULL);
    free(ao);
}
