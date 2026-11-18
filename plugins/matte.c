/* A field of one colour: a matte to key over or fade to, in any colour and not only the background's nine. The
 * colour is the stream's argument, so this is also the example of a plug-in that asks for one. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "tsplugin.h"

#define FPS 30
#define HANDOUTS 3              /* a picture goes to each bus that shows it, then there is none until the next */

struct matte {
    uint32_t rgb;
    double t0;
    long picture;               /* the picture handed out last */
    int handed;                 /* how many times */
};

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

/* arg: the colour as six hexadecimal digits, red first */
static void *matte_open(const char *arg)
{
    struct matte *m;
    char *end;
    unsigned long rgb = strtoul(arg, &end, 16);
    if (strlen(arg) != 6 || *end || !(m = calloc(1, sizeof(*m)))) return NULL;
    m->rgb = rgb;
    m->t0 = now();
    m->picture = -1;
    return m;
}

static void matte_close(void *ctx)
{
    free(ctx);
}

static int matte_count(void *ctx)
{
    (void)ctx;
    return 1;
}

static int matte_source_name(void *ctx, int source, char *buf, int len)
{
    struct matte *m = ctx;
    if (source) return -1;
    snprintf(buf, len, "Matte %06lx", (unsigned long)m->rgb);
    return 0;
}

static int matte_grab(void *ctx, int source, uint32_t *dst, int w, int h)
{
    struct matte *m = ctx;
    long n = (long)((now() - m->t0) * FPS);
    size_t i, count = (size_t)w * h;
    if (source) return -1;
    if (n == m->picture && m->handed >= HANDOUTS) return 1;
    if (n != m->picture) m->handed = 0;
    m->picture = n;
    m->handed++;
    for (i = 0; i < count; i++) dst[i] = m->rgb;
    return 0;
}

static const struct tsi_backend matte_backend = {
    TSI_API_VERSION, "matte", matte_open, matte_close, matte_count, matte_source_name, matte_grab, NULL, 0
};

static const struct tsp_input inputs[] = {
    { &matte_backend, "Colour matte", "Colour as six hexadecimal digits, red first\n(ff0000 is red, 808080 a middle grey)" }
};

TSP_IDENTITY("matte.tsplugin", 1, 0, "18.11.2026");

const struct tsp_plugin tsp_plugin = {
    TSP_API_VERSION, "Colour matte", "A field of one colour, as a stream for any input", inputs, 1, NULL, 0
};
