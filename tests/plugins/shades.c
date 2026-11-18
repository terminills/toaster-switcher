/* Fourteen greys, each a stream: more than the Stream list shows at once, for testing its scroller. Not for
 * Toaster:Plugins on a machine in use. */
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "tsplugin.h"

#define FPS 30
#define HANDOUTS 3
#define SHADES 14

struct shades {
    double t0;
    long picture[SHADES];
    int handed[SHADES];
};

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

static void *shades_open(const char *arg)
{
    struct shades *s = calloc(1, sizeof(*s));
    int i;
    (void)arg;
    if (!s) return NULL;
    s->t0 = now();
    for (i = 0; i < SHADES; i++) s->picture[i] = -1;
    return s;
}

static void shades_close(void *ctx)
{
    free(ctx);
}

static int shades_count(void *ctx)
{
    (void)ctx;
    return SHADES;
}

static int shades_source_name(void *ctx, int source, char *buf, int len)
{
    (void)ctx;
    if (source < 0 || source >= SHADES) return -1;
    snprintf(buf, len, "Shade %d", source + 1);
    return 0;
}

static int shades_grab(void *ctx, int source, uint32_t *dst, int w, int h)
{
    struct shades *s = ctx;
    long n = (long)((now() - s->t0) * FPS);
    uint32_t grey = source >= 0 && source < SHADES ? (uint32_t)(source + 1) * 255 / SHADES * 0x010101 : 0;
    size_t i, count = (size_t)w * h;
    if (source < 0 || source >= SHADES) return -1;
    if (n == s->picture[source] && s->handed[source] >= HANDOUTS) return 1;
    if (n != s->picture[source]) s->handed[source] = 0;
    s->picture[source] = n;
    s->handed[source]++;
    for (i = 0; i < count; i++) dst[i] = grey;
    return 0;
}

static const struct tsi_backend shades_backend = {
    TSI_API_VERSION, "shades", shades_open, shades_close, shades_count, shades_source_name, shades_grab, NULL, 0
};

static const struct tsp_input inputs[] = {
    { &shades_backend, "Shades", NULL }
};

TSP_IDENTITY("shades.tsplugin", 1, 0, "18.11.2026");

const struct tsp_plugin tsp_plugin = {
    TSP_API_VERSION, "Shades", "Fourteen greys, for testing a list of streams that scrolls", inputs, 1, NULL, 0
};
