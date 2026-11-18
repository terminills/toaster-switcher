/* The outputs (see outputs.h): a sink (sink.h) each, and a hold on the plug-in whose backend it runs. */
#include <string.h>
#include "sink.h"
#include "plugins.h"
#include "outputs.h"

static struct output {
    struct sink sink;
    const struct tso_backend *be;       /* held while the sink may call it */
    int failed;                         /* it ended by itself, or never opened */
} outputs[MAXOUTPUTS];
static int picw, pich;

void out_init(int w, int h)
{
    picw = w;
    pich = h;
}

/* An output whose sink has ended lets go of its plug-in. */
static void settle(struct output *o)
{
    if (!o->be || sink_active(&o->sink)) return;
    sink_stop(&o->sink);
    if (o->sink.state != SINK_ENDED) o->failed = 1;
    plugins_release_output(o->be);
    o->be = NULL;
}

int out_start(int n, const char *spec)
{
    struct output *o = &outputs[n];
    const char *colon = strchr(spec, ':');
    char name[16];
    if (n < 0 || n >= MAXOUTPUTS || !picw) return -1;
    out_stop(n);
    o->failed = 1;
    if (!colon || colon == spec || colon - spec >= (int)sizeof(name)) return -1;
    memcpy(name, spec, colon - spec);
    name[colon - spec] = 0;
    if (sink_init(&o->sink, picw, pich) || !(o->be = plugins_hold_output(name))) return -1;
    o->failed = 0;
    if (sink_start(&o->sink, o->be, colon + 1, 0, 0)) settle(o);
    return 0;
}

void out_stop(int n)
{
    struct output *o = &outputs[n];
    if (n < 0 || n >= MAXOUTPUTS) return;
    sink_stop(&o->sink);
    settle(o);
    o->failed = 0;
}

void out_exit(void)
{
    int n;
    for (n = 0; n < MAXOUTPUTS; n++) out_stop(n);
}

int out_state(int n)
{
    struct output *o = &outputs[n];
    if (n < 0 || n >= MAXOUTPUTS) return OUT_OFF;
    settle(o);
    if (o->failed) return OUT_FAILED;
    if (!o->be) return OUT_OFF;
    return o->sink.state == SINK_RUNNING ? OUT_ON : OUT_STARTING;
}

int out_active(void)
{
    int n;
    for (n = 0; n < MAXOUTPUTS; n++)
        if (sink_active(&outputs[n].sink)) return 1;
    return 0;
}

void out_video(const uint32_t *rgb)
{
    int n;
    for (n = 0; n < MAXOUTPUTS; n++) sink_video(&outputs[n].sink, rgb);
}

void out_audio(const int16_t *pcm, int frames)
{
    int n;
    for (n = 0; n < MAXOUTPUTS; n++) sink_audio(&outputs[n].sink, pcm, frames);
}
