/* PluginInfo: what a Toaster plug-in (tsplugin.h) offers, and whether its streams deliver, without the Switcher.
 *
 * PluginInfo FILE [ARG] [SECONDS]      e.g.  PluginInfo Toaster:Plugins/bars.tsplugin "" 2
 *
 * The plug-in is opened and described. With SECONDS, every kind of stream it has is opened with ARG from a thread,
 * as the Switcher does, and its first source is grabbed at 480x360 for that long: pictures, how many differ from
 * the one before, frames of sound and the loudest of them. Then the plug-in is closed, and it says whether it left
 * memory. */
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <aros/libcall.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/stdc.h>
#include <proto/stdcio.h>
#include <proto/posixc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include "tsplugin.h"

#define W 480
#define H 360

struct job {
    const struct tsi_backend *be;
    const char *arg;
    double seconds;
    int opened, sources;
    char name[64];
    long pictures, changed, sound;
    int peak;
    uint32_t middle;            /* the last picture's pixel in the middle of the upper half */
};

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

static void *grab(void *arg)
{
    static int16_t snd[4096 * 2];
    struct job *j = arg;
    uint32_t *pic = malloc(W * H * 4), *last = calloc(W * H, 4);
    void *ctx = pic && last ? j->be->open(j->arg) : NULL;
    double end = now() + j->seconds;
    if (ctx) {
        j->opened = 1;
        j->sources = j->be->count(ctx);
        if (j->sources > 0 && j->be->source_name(ctx, 0, j->name, sizeof(j->name))) strcpy(j->name, "?");
        while (j->sources > 0 && now() < end) {
            int n, i;
            if (j->be->grab(ctx, 0, pic, W, H)) {
                usleep(5000);
                continue;
            }
            j->pictures++;
            if (memcmp(pic, last, W * H * 4)) j->changed++;
            memcpy(last, pic, W * H * 4);
            j->middle = pic[H / 4 * W + W / 2];
            while (j->be->audio && (n = j->be->audio(ctx, 0, snd, 4096)) > 0) {
                j->sound += n;
                for (i = 0; i < n * 2; i++)
                    if (abs(snd[i]) > j->peak) j->peak = abs(snd[i]);
                if (n < 4096) break;
            }
        }
        j->be->close(ctx);
    }
    free(pic);
    free(last);
    return NULL;
}

int main(int argc, char **argv)
{
    struct Library *base;
    const struct tsp_plugin *p;
    struct tsp_host host;
    const char *file, *arg = argc > 2 ? argv[2] : "";
    double seconds = argc > 3 ? atof(argv[3]) : 0;
    char name[108];
    int i, rc = 0, there;

    if (argc < 2 || argc > 4) {
        printf("usage: PluginInfo FILE [ARG] [SECONDS]\n");
        return 20;
    }
    file = argv[1];
    if (!(base = OpenLibrary((CONST_STRPTR)file, 0))) {
        printf("%s: does not open as a library\n", file);
        return 10;
    }
    strncpy(name, (const char *)base->lib_Node.ln_Name, sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    host.api_version = TSP_API_VERSION;
    host.stdc = __aros_getbase_StdCBase();
    host.stdcio = __aros_getbase_StdCIOBase();
    host.posixc = __aros_getbase_PosixCBase();
    p = TSP_DESCRIBE(base, &host);
    if (!p) printf("%s %d.%d: not a plug-in for this interface (%d)\n", name, base->lib_Version, base->lib_Revision,
                   TSP_API_VERSION);
    else printf("%s %d.%d: plug-in interface %lu%s\n", name, base->lib_Version, base->lib_Revision,
                (unsigned long)p->api_version, p->api_version == TSP_API_VERSION ? "" : " (not this one's)");
    if (p && p->api_version == TSP_API_VERSION) {
        printf("  %s: %s\n", p->name, p->about);
        for (i = 0; i < p->ninputs; i++) {
            const struct tsp_input *in = &p->inputs[i];
            printf("  stream \"%s\": %s, interface %lu%s%s%s\n", in->backend->name, in->label,
                   (unsigned long)in->backend->api_version, in->backend->api_version == TSI_API_VERSION ? "" : " (not this one's)",
                   in->ask ? ", asks: " : "", in->ask ? in->ask : "");
            if (seconds > 0 && in->backend->api_version == TSI_API_VERSION) {
                struct job j;
                pthread_t t;
                memset(&j, 0, sizeof(j));
                j.be = in->backend;
                j.arg = arg;
                j.seconds = seconds;
                if (pthread_create(&t, NULL, grab, &j)) {
                    printf("    no thread\n");
                    rc = 10;
                    continue;
                }
                pthread_join(t, NULL);
                if (!j.opened) {
                    printf("    does not open with \"%s\"\n", arg);
                    rc = 5;
                } else {
                    printf("    %d source%s, the first \"%s\": %ld pictures in %.1f s, %ld changed, middle %06lx, "
                           "%ld frames of sound, peak %d\n", j.sources, j.sources == 1 ? "" : "s", j.name, j.pictures,
                           seconds, j.changed, (unsigned long)(j.middle & 0xffffff), j.sound, j.peak);
                }
            }
        }
        for (i = 0; i < p->noutputs; i++) {
            const struct tsp_output *out = &p->outputs[i];
            printf("  output \"%s\": %s, interface %lu%s%s%s\n", out->backend->name, out->label,
                   (unsigned long)out->backend->api_version, out->backend->api_version == TSO_API_VERSION ? "" : " (not this one's)",
                   out->ask ? ", asks: " : "", out->ask ? out->ask : "");
        }
    } else {
        rc = 5;
    }
    CloseLibrary(base);
    Forbid();
    there = FindName(&SysBase->LibList, (CONST_STRPTR)name) != NULL;
    Permit();
    printf("closed: %s\n", there ? "still in memory" : "left memory");
    return rc;
}
