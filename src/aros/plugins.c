/* The Switcher's plug-ins (see plugins.h): each is a library, opened by its path, that hands out one table. */
#include <exec/libraries.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <aros/libcall.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/stdc.h>
#include <proto/stdcio.h>
#include <proto/posixc.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "plugins.h"

struct plugin {
    struct Library *base;
    const struct tsp_plugin *p;
    char file[40];
    int uses;
};

static struct plugin plugins[MAXPLUGINS];
static int nplugins, ready;
static struct SignalSemaphore lock;

void plugins_init(void)
{
    if (!ready) InitSemaphore(&lock);
    ready = 1;
}

/* Close the plug-ins nothing uses (all of them with force). Under lock. */
static void close_unused(int force)
{
    int i, k = 0;
    for (i = 0; i < nplugins; i++) {
        if (plugins[i].uses && !force) plugins[k++] = plugins[i];
        else CloseLibrary(plugins[i].base);
    }
    nplugins = k;
}

void plugins_exit(void)
{
    if (!ready) return;
    ObtainSemaphore(&lock);
    close_unused(1);
    ReleaseSemaphore(&lock);
}

static int is_plugin_name(const char *name)
{
    size_t n = strlen(name), s = strlen(TSP_SUFFIX), i;
    if (n <= s || n >= sizeof(plugins[0].file)) return 0;
    for (i = 0; i < s; i++)
        if (tolower((unsigned char)name[n - s + i]) != TSP_SUFFIX[i]) return 0;
    return 1;
}

static void load(const char *file)
{
    char path[sizeof(TSP_DRAWER) + sizeof(plugins[0].file) + 1];
    struct Library *base;
    const struct tsp_plugin *p;
    struct tsp_host host;
    int i;
    for (i = 0; i < nplugins; i++)
        if (!strcasecmp(plugins[i].file, file)) return;    /* in use, and kept */
    if (nplugins == MAXPLUGINS) return;
    snprintf(path, sizeof(path), "%s/%s", TSP_DRAWER, file);
    if (!(base = OpenLibrary((CONST_STRPTR)path, 0))) return;
    host.api_version = TSP_API_VERSION;
    host.stdc = __aros_getbase_StdCBase();              /* the plug-in runs on this program's C library */
    host.stdcio = __aros_getbase_StdCIOBase();
    host.posixc = __aros_getbase_PosixCBase();
    p = TSP_DESCRIBE(base, &host);
    if (!p || p->api_version != TSP_API_VERSION) {
        CloseLibrary(base);
        return;
    }
    plugins[nplugins].base = base;
    plugins[nplugins].p = p;
    plugins[nplugins].uses = 0;
    strcpy(plugins[nplugins++].file, file);
}

int plugins_scan(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR win = me->pr_WindowPtr;
    struct FileInfoBlock *fib;
    BPTR dir;
    if (!ready) return 0;
    me->pr_WindowPtr = (APTR)-1;        /* without the drawer there are none; its volume is not asked for */
    ObtainSemaphore(&lock);
    close_unused(0);
    if ((dir = Lock((CONST_STRPTR)TSP_DRAWER, SHARED_LOCK))) {
        if ((fib = AllocDosObject(DOS_FIB, NULL))) {
            if (Examine(dir, fib))
                while (ExNext(dir, fib))
                    if (fib->fib_DirEntryType < 0 && is_plugin_name((const char *)fib->fib_FileName))
                        load((const char *)fib->fib_FileName);
            FreeDosObject(DOS_FIB, fib);
        }
        UnLock(dir);
    }
    ReleaseSemaphore(&lock);
    me->pr_WindowPtr = win;
    return nplugins;
}

int plugins_info(int index, struct plugin_info *out)
{
    const struct plugin *pl = &plugins[index];
    if (index < 0 || index >= nplugins) return -1;
    memset(out, 0, sizeof(*out));
    strcpy(out->file, pl->file);
    snprintf(out->name, sizeof(out->name), "%s", pl->p->name ? pl->p->name : pl->file);
    snprintf(out->about, sizeof(out->about), "%s", pl->p->about ? pl->p->about : "");
    out->version = pl->base->lib_Version;
    out->revision = pl->base->lib_Revision;
    out->ninputs = pl->p->ninputs;
    out->noutputs = pl->p->noutputs;
    out->uses = pl->uses;
    return 0;
}

/* A backend's name is the first part of a spec ("name:arg:source"). */
static int name_ok(const char *name)
{
    return name && name[0] && strlen(name) < 16 && !strchr(name, ':');
}

const struct tsp_input *plugins_input_kind(int index)
{
    int i, k;
    for (i = 0; i < nplugins; i++)
        for (k = 0; k < plugins[i].p->ninputs; k++) {
            const struct tsp_input *in = &plugins[i].p->inputs[k];
            if (!in->backend || in->backend->api_version != TSI_API_VERSION || !name_ok(in->backend->name)) continue;
            if (!index--) return in;
        }
    return NULL;
}

const struct tsp_output *plugins_output_kind(int index)
{
    int i, k;
    for (i = 0; i < nplugins; i++)
        for (k = 0; k < plugins[i].p->noutputs; k++) {
            const struct tsp_output *out = &plugins[i].p->outputs[k];
            if (!out->backend || out->backend->api_version != TSO_API_VERSION || !name_ok(out->backend->name)) continue;
            if (!index--) return out;
        }
    return NULL;
}

const struct tsi_backend *plugins_hold_input(const char *name)
{
    const struct tsi_backend *be = NULL;
    int i, k;
    if (!ready) return NULL;
    ObtainSemaphore(&lock);
    for (i = 0; i < nplugins && !be; i++)
        for (k = 0; k < plugins[i].p->ninputs && !be; k++) {
            const struct tsi_backend *b = plugins[i].p->inputs[k].backend;
            if (b && b->api_version == TSI_API_VERSION && name_ok(b->name) && !strcmp(b->name, name)) {
                plugins[i].uses++;
                be = b;
            }
        }
    ReleaseSemaphore(&lock);
    return be;
}

const struct tso_backend *plugins_hold_output(const char *name)
{
    const struct tso_backend *be = NULL;
    int i, k;
    if (!ready) return NULL;
    ObtainSemaphore(&lock);
    for (i = 0; i < nplugins && !be; i++)
        for (k = 0; k < plugins[i].p->noutputs && !be; k++) {
            const struct tso_backend *b = plugins[i].p->outputs[k].backend;
            if (b && b->api_version == TSO_API_VERSION && name_ok(b->name) && !strcmp(b->name, name)) {
                plugins[i].uses++;
                be = b;
            }
        }
    ReleaseSemaphore(&lock);
    return be;
}

void plugins_release_input(const struct tsi_backend *be)
{
    int i, k;
    if (!ready || !be) return;
    ObtainSemaphore(&lock);
    for (i = 0; i < nplugins; i++)
        for (k = 0; k < plugins[i].p->ninputs; k++)
            if (plugins[i].p->inputs[k].backend == be && plugins[i].uses > 0) plugins[i].uses--;
    ReleaseSemaphore(&lock);
}

void plugins_release_output(const struct tso_backend *be)
{
    int i, k;
    if (!ready || !be) return;
    ObtainSemaphore(&lock);
    for (i = 0; i < nplugins; i++)
        for (k = 0; k < plugins[i].p->noutputs; k++)
            if (plugins[i].p->outputs[k].backend == be && plugins[i].uses > 0) plugins[i].uses--;
    ReleaseSemaphore(&lock);
}
