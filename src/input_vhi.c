/* VHI input backend. By default every UVC device (a camera, an HDMI capture dongle) is a switcher source: uvc.class
 * publishes a VHI library for each one it binds, uvc0.vhi, uvc1.vhi, ... ("uvc" as the driver).
 * Any other driver is one device whose inputs are the sources. A source's
 * sound is the AHI recording mode of the same USB device (usbaudio.class names its modes "<product>: ..."): it is
 * recorded through AHI once asked for, into a ring at the device's rate, and handed out at TSI_AUDIO_RATE. */
#include <exec/types.h>
#include <exec/memory.h>
#include <aros/libcall.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <vhi/vhi.h>
#include <devices/ahi.h>
#include <proto/ahi.h>
#include <aros/asmcall.h>
#include <utility/hooks.h>
#include <string.h>
#include "tsinput.h"

#define MAXSRC 16
#define SRING 65536             /* recorded stereo frames per source (power of two) */

struct vhi_sound {              /* one source's recording */
    struct AHIAudioCtrl *ctrl;
    struct Hook hook;
    int16_t *ring;              /* SRING stereo frames, written by the record hook */
    volatile uint32_t w;        /* frames written (hook), read position below */
    uint32_t r;
    ULONG rate;                 /* the device's rate */
    uint32_t frac;              /* resampling position between r and r + 1, 16.16 */
    int tried;                  /* looked for its mode already */
};

struct vhi_src {
    struct Library *base;       /* the source's driver */
    APTR handle;                /* the VHI device the source is on */
    int input;                  /* its input there */
    char name[128];
};

struct vhi_ctx {
    struct Library *base;       /* the named driver, NULL when the sources are the UVC devices */
    struct vhi_src src[MAXSRC];
    int nsrc;
    APTR shared;                /* the named driver's one device, whose inputs are the sources */
    int current;                /* input selected on the shared device, -1 none */
    struct MsgPort *ahiport;
    struct AHIRequest *ahireq;  /* opened on AHI_NO_UNIT for the AHI library calls */
    struct vhi_sound snd[MAXSRC];
};

struct Library *AHIBase;

/* VHI drivers all export vhi_api as their first function; the client picks the library at runtime. */
static IPTR call_vhi(struct Library *VHIBase, ULONG method, ULONG submethod, APTR attr, ULONG *error, APTR handle)
{
    return AROS_LC5(IPTR, vhi_api,
                    AROS_LCA(ULONG, method, D2),
                    AROS_LCA(ULONG, submethod, D3),
                    AROS_LCA(APTR, attr, D4),
                    AROS_LCA(ULONG *, error, D5),
                    AROS_LCA(APTR, handle, D6),
                    struct Library *, VHIBase, 5, Vhi);
}

static void copy_name(char *dst, const char *s)
{
    strncpy(dst, s, sizeof(((struct vhi_src *)0)->name) - 1);
    dst[sizeof(((struct vhi_src *)0)->name) - 1] = 0;
}

/* the UVC devices: uvc.class numbers their libraries from 0 and keeps a device's number while it is unplugged */
static void open_uvc_devices(struct vhi_ctx *c)
{
    int unit;
    for (unit = 0; unit < MAXSRC && c->nsrc < MAXSRC; unit++) {
        struct vhi_src *s = &c->src[c->nsrc];
        char lib[16];
        ULONG err = 0;
        STRPTR card;
        strcpy(lib, "uvc0.vhi");
        if (unit > 9) strcpy(lib, "uvc10.vhi");
        lib[unit > 9 ? 4 : 3] = '0' + unit % 10;
        if (!(s->base = OpenLibrary((CONST_STRPTR)lib, 0))) continue;
        if (!(s->handle = (APTR)call_vhi(s->base, VHI_METHOD_OPEN, 0, NULL, &err, NULL))) {
            CloseLibrary(s->base);
            continue;
        }
        s->input = 0;
        card = (STRPTR)call_vhi(s->base, VHI_METHOD_GET, VHI_CARD_NAME, NULL, &err, s->handle);
        copy_name(s->name, card ? (const char *)card : lib);
        c->nsrc++;
    }
}

/* a named driver: one device, its inputs are the sources */
static int open_shared_device(struct vhi_ctx *c)
{
    ULONG err = 0;
    IPTR n;
    int i;
    c->shared = (APTR)call_vhi(c->base, VHI_METHOD_OPEN, 0, NULL, &err, NULL);
    if (!c->shared) return -1;
    n = call_vhi(c->base, VHI_METHOD_GET, VHI_NUMBER_OF_INPUTS, NULL, &err, c->shared);
    if (err) n = 0;
    for (i = 0; i < (int)n && c->nsrc < MAXSRC; i++) {
        STRPTR s;
        err = 0;
        s = (STRPTR)call_vhi(c->base, VHI_METHOD_GET, VHI_NAME_OF_INPUT, (APTR)(IPTR)i, &err, c->shared);
        c->src[c->nsrc].base = c->base;
        c->src[c->nsrc].handle = c->shared;
        c->src[c->nsrc].input = i;
        copy_name(c->src[c->nsrc].name, !err && s ? (const char *)s : "");
        if (!err && s) FreeVec(s);
        c->nsrc++;
    }
    return 0;
}

static void *vhi_open(const char *driver)
{
    struct vhi_ctx *c = AllocVec(sizeof(*c), MEMF_ANY | MEMF_CLEAR);
    if (!c) return NULL;
    c->current = -1;
    /* "uvc.library" is what input settings saved before the per-device libraries call them */
    if (!driver || !*driver || !strcmp(driver, "uvc") || !strcmp(driver, "uvc.library")) {
        open_uvc_devices(c);
        if (c->nsrc) return c;
    } else if ((c->base = OpenLibrary((CONST_STRPTR)driver, 0))) {
        if (!open_shared_device(c)) return c;
        CloseLibrary(c->base);
    }
    FreeVec(c);
    return NULL;
}

static void vhi_close(void *ctx)
{
    struct vhi_ctx *c = ctx;
    ULONG err = 0;
    int i;
    for (i = 0; i < MAXSRC; i++) {
        if (c->snd[i].ctrl) {
            AHI_ControlAudio(c->snd[i].ctrl, AHIC_Record, FALSE, TAG_DONE);
            AHI_FreeAudio(c->snd[i].ctrl);
        }
        if (c->snd[i].ring) FreeVec(c->snd[i].ring);
    }
    if (c->ahireq) {
        CloseDevice((struct IORequest *)c->ahireq);
        DeleteIORequest((struct IORequest *)c->ahireq);
    }
    if (c->ahiport) DeleteMsgPort(c->ahiport);
    for (i = 0; i < c->nsrc; i++)
        if (c->src[i].handle != c->shared) {
            call_vhi(c->src[i].base, VHI_METHOD_CLOSE, 0, NULL, &err, c->src[i].handle);
            CloseLibrary(c->src[i].base);
        }
    if (c->shared) call_vhi(c->base, VHI_METHOD_CLOSE, 0, NULL, &err, c->shared);
    if (c->base) CloseLibrary(c->base);
    FreeVec(c);
}

static int vhi_count(void *ctx)
{
    return ((struct vhi_ctx *)ctx)->nsrc;
}

static int vhi_source_name(void *ctx, int source, char *buf, int len)
{
    struct vhi_ctx *c = ctx;
    if (source < 0 || source >= c->nsrc || len < 1) return -1;
    strncpy(buf, c->src[source].name, len - 1);
    buf[len - 1] = 0;
    return 0;
}

/* only the shared device has inputs to choose between */
static int vhi_select(struct vhi_ctx *c, struct vhi_src *s)
{
    struct vhi_setoptions opt;
    ULONG err = 0;
    if (s->handle != c->shared || c->nsrc < 2 || c->current == s->input) return 0;
    opt.option = VHI_OPT_INPUT;
    opt.value = s->input;
    call_vhi(c->base, VHI_METHOD_SET, VHI_OPTIONS, &opt, &err, c->shared);
    if (err) return -1;
    c->current = s->input;
    return 0;
}

static int vhi_grab(void *ctx, int source, uint32_t *dst, int w, int h)
{
    struct vhi_ctx *c = ctx;
    struct vhi_digitize dig;
    struct vhi_image *img;
    struct vhi_src *src;
    ULONG err = 0;
    int x, y;

    if (source < 0 || source >= c->nsrc) return -1;
    src = &c->src[source];
    if (vhi_select(c, src)) return -1;
    /* the driver picks its size nearest the one the picture is scaled to */
    memset(&dig, 0, sizeof(dig));
    dig.dim.x2 = dig.dim.dst_width = (ULONG)w;
    dig.dim.y2 = dig.dim.dst_height = (ULONG)h;
    img = (struct vhi_image *)call_vhi(src->base, VHI_METHOD_PERFORM, VHI_DIGITIZE_PICTURE, &dig, &err, src->handle);
    if (err || !img) return -1;
    if (img->chunky && img->width && img->height && (img->type == VHI_RGB_24 || img->type == VHI_GRAYSCALE_8)) {
        const UBYTE *p = img->chunky;
        int bpp = img->type == VHI_RGB_24 ? 3 : 1;
        for (y = 0; y < h; y++) {
            const UBYTE *row = p + (size_t)(y * img->height / h) * img->width * bpp;
            for (x = 0; x < w; x++) {
                const UBYTE *s = row + (size_t)(x * img->width / w) * bpp;
                dst[y * w + x] = bpp == 3 ? (uint32_t)s[0] << 16 | s[1] << 8 | s[2] : (uint32_t)s[0] * 0x010101u;
            }
        }
    } else {
        err = 1;
    }
    if (img->chunky) FreeVec(img->chunky);
    FreeVec(img);
    return err ? -1 : 0;
}

/* The record hook (driver context): copy the block into the source's ring. */
AROS_UFH3(IPTR, vhi_record, AROS_UFHA(struct Hook *, h, A0), AROS_UFHA(struct AHIAudioCtrl *, actrl, A2),
          AROS_UFHA(struct AHIRecordMessage *, msg, A1))
{
    AROS_USERFUNC_INIT
    struct vhi_sound *sd = h->h_Data;
    const int16_t *in = msg->ahirm_Buffer;
    uint32_t i, w = sd->w;
    (void)actrl;
    if (msg->ahirm_Type == AHIST_S16S)
        for (i = 0; i < msg->ahirm_Length; i++, w++) {
            sd->ring[(w & (SRING - 1)) * 2] = in[i * 2];
            sd->ring[(w & (SRING - 1)) * 2 + 1] = in[i * 2 + 1];
        }
    else if (msg->ahirm_Type == AHIST_M16S)
        for (i = 0; i < msg->ahirm_Length; i++, w++)
            sd->ring[(w & (SRING - 1)) * 2] = sd->ring[(w & (SRING - 1)) * 2 + 1] = in[i];
    sd->w = w;
    return 0;
    AROS_USERFUNC_EXIT
}

/* Start recording source's device: the recording AHI mode whose name begins with the source's name. */
static void sound_start(struct vhi_ctx *c, int source)
{
    struct vhi_sound *sd = &c->snd[source];
    char name[64], mode[128];
    ULONG id = AHI_INVALID_ID, rec, freq = 0;
    size_t n;
    sd->tried = 1;
    if (vhi_source_name(c, source, name, sizeof(name))) return;
    if (!c->ahireq) {
        if (!(c->ahiport = CreateMsgPort()) ||
            !(c->ahireq = (struct AHIRequest *)CreateIORequest(c->ahiport, sizeof(struct AHIRequest))))
            return;
        c->ahireq->ahir_Version = 4;
        if (OpenDevice((CONST_STRPTR)AHINAME, AHI_NO_UNIT, (struct IORequest *)c->ahireq, 0)) {
            DeleteIORequest((struct IORequest *)c->ahireq);
            c->ahireq = NULL;
            return;
        }
        AHIBase = (struct Library *)c->ahireq->ahir_Std.io_Device;
    }
    n = strlen(name) > 20 ? 20 : strlen(name);      /* usbaudio.class keeps 20 characters of the product */
    while ((id = AHI_NextAudioID(id)) != AHI_INVALID_ID) {
        rec = FALSE;
        mode[0] = 0;
        AHI_GetAudioAttrs(id, NULL, AHIDB_Name, (IPTR)mode, AHIDB_BufferLen, sizeof(mode), AHIDB_Record, (IPTR)&rec,
                          TAG_DONE);
        if (rec && !strncmp(mode, name, n) && mode[n] == ':') break;
    }
    if (id == AHI_INVALID_ID || !(sd->ring = AllocVec(SRING * 4, MEMF_ANY | MEMF_CLEAR))) return;
    sd->hook.h_Entry = (HOOKFUNC)(void (*)(void))vhi_record;
    sd->hook.h_Data = sd;
    sd->ctrl = AHI_AllocAudio(AHIA_AudioID, id, AHIA_MixFreq, TSI_AUDIO_RATE, AHIA_Channels, 1, AHIA_Sounds, 1,
                              AHIA_RecordFunc, (IPTR)&sd->hook, TAG_DONE);
    if (!sd->ctrl) return;
    AHI_ControlAudio(sd->ctrl, AHIC_MixFreq_Query, (IPTR)&freq, TAG_DONE);
    sd->rate = freq ? freq : TSI_AUDIO_RATE;
    sd->r = sd->w = 0;
    if (AHI_ControlAudio(sd->ctrl, AHIC_Record, TRUE, TAG_DONE)) {
        AHI_FreeAudio(sd->ctrl);
        sd->ctrl = NULL;
    }
}

/* What was recorded since the last call, resampled to TSI_AUDIO_RATE (linear). */
static int vhi_audio(void *ctx, int source, int16_t *dst, int frames)
{
    struct vhi_ctx *c = ctx;
    struct vhi_sound *sd;
    uint32_t w, step;
    int n = 0, ch;
    if (source < 0 || source >= MAXSRC) return 0;
    sd = &c->snd[source];
    if (!sd->tried) sound_start(c, source);
    if (!sd->ctrl) return 0;
    w = sd->w;
    if (w - sd->r > SRING / 2) sd->r = w - SRING / 4;  /* fell far behind: keep the newest */
    step = (uint32_t)(((uint64_t)sd->rate << 16) / TSI_AUDIO_RATE);
    while (n < frames && w - sd->r >= 2) {
        uint32_t a = sd->r & (SRING - 1), b = (sd->r + 1) & (SRING - 1);
        for (ch = 0; ch < 2; ch++)
            dst[n * 2 + ch] = (int16_t)(sd->ring[a * 2 + ch] +
                                        (((int32_t)sd->ring[b * 2 + ch] - sd->ring[a * 2 + ch]) * (int32_t)sd->frac >> 16));
        n++;
        sd->frac += step;
        sd->r += sd->frac >> 16;
        sd->frac &= 0xffff;
    }
    return n;
}

const struct tsi_backend tsi_vhi = {
    TSI_API_VERSION, "vhi", vhi_open, vhi_close, vhi_count, vhi_source_name, vhi_grab, vhi_audio, -1
};
