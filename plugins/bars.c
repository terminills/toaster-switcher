/* Colour bars with a 1 kHz tone: a stream to line a monitor up with, and to try an output without a camera. It is
 * also the smallest plug-in there is, to start another from. */
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "tsplugin.h"

#define FPS 30
#define HANDOUTS 3              /* a picture goes to each bus that shows it, then there is none until the next */

struct bars {
    double t0;
    long picture;               /* the picture handed out last */
    int handed;                 /* how many times */
    long sound;                 /* frames of sound handed out */
};

/* one period of 1 kHz at TSI_AUDIO_RATE, 20 dB below full scale */
static const int16_t tone[48] = {
    0, 428, 848, 1254, 1639, 1995, 2317, 2600, 2838, 3028, 3165, 3249, 3277, 3249, 3165, 3028, 2838, 2600, 2317, 1995,
    1639, 1254, 848, 428, 0, -428, -848, -1254, -1639, -1995, -2317, -2600, -2838, -3028, -3165, -3249, -3277, -3249,
    -3165, -3028, -2838, -2600, -2317, -1995, -1639, -1254, -848, -428
};

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

static void *bars_open(const char *arg)
{
    struct bars *b = calloc(1, sizeof(*b));
    (void)arg;
    if (!b) return NULL;
    b->t0 = now();
    b->picture = -1;
    return b;
}

static void bars_close(void *ctx)
{
    free(ctx);
}

static int bars_count(void *ctx)
{
    (void)ctx;
    return 1;
}

static int bars_source_name(void *ctx, int source, char *buf, int len)
{
    (void)ctx;
    if (source || len < 1) return -1;
    strncpy(buf, "Colour bars", len - 1);
    buf[len - 1] = 0;
    return 0;
}

/* The upper two thirds are the seven bars at 75%; below them black, with a white stripe that crosses the picture
 * every two seconds, so a picture that stands still shows. */
static int bars_grab(void *ctx, int source, uint32_t *dst, int w, int h)
{
    static const uint32_t bar[7] = { 0xbfbfbf, 0xbfbf00, 0x00bfbf, 0x00bf00, 0xbf00bf, 0xbf0000, 0x0000bf };
    struct bars *b = ctx;
    long n = (long)((now() - b->t0) * FPS);
    int x, y, split = h * 2 / 3, stripe = (int)(n % (2 * FPS) * w / (2 * FPS)), sw = w / 40 + 1;
    if (source) return -1;
    if (n == b->picture && b->handed >= HANDOUTS) return 1;
    if (n != b->picture) b->handed = 0;
    b->picture = n;
    b->handed++;
    for (x = 0; x < w; x++) dst[x] = bar[x * 7 / w];
    for (y = 1; y < split; y++) memcpy(dst + (size_t)y * w, dst, (size_t)w * 4);
    if (split < h) {
        uint32_t *row = dst + (size_t)split * w;
        for (x = 0; x < w; x++) row[x] = x >= stripe && x < stripe + sw ? 0xffffff : 0;
        for (y = split + 1; y < h; y++) memcpy(dst + (size_t)y * w, row, (size_t)w * 4);
    }
    return 0;
}

static int bars_audio(void *ctx, int source, int16_t *dst, int frames)
{
    struct bars *b = ctx;
    long due = (long)((now() - b->t0) * TSI_AUDIO_RATE) - b->sound;
    int i;
    if (source) return 0;
    if (due > TSI_AUDIO_RATE / 2) {             /* nobody listened for a while: only the last half second */
        b->sound += due - TSI_AUDIO_RATE / 2;
        due = TSI_AUDIO_RATE / 2;
    }
    if (frames > due) frames = due;
    for (i = 0; i < frames; i++)
        dst[i * 2] = dst[i * 2 + 1] = tone[(b->sound + i) % 48];
    b->sound += frames;
    return frames;
}

static const struct tsi_backend bars_backend = {
    TSI_API_VERSION, "bars", bars_open, bars_close, bars_count, bars_source_name, bars_grab, bars_audio, 0
};

static const struct tsp_input inputs[] = {
    { &bars_backend, "Colour bars", NULL }
};

TSP_IDENTITY("bars.tsplugin", 1, 0, "18.11.2026");

const struct tsp_plugin tsp_plugin = {
    TSP_API_VERSION, "Colour bars", "Colour bars with a 1 kHz tone, as a stream for any input", inputs, 1, NULL, 0
};
