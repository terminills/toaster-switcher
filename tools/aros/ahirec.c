/* ahirec MODE/N SECONDS/N: record an AHI mode and report how many frames arrived per second, their level, and how many
 * blocks were an exact repeat of the block before (a live input never repeats a whole block: a repeat is data delivered
 * twice, e.g. a USB packet overwritten before it was copied). */
#include <exec/memory.h>
#include <devices/ahi.h>
#include <aros/asmcall.h>
#include <utility/hooks.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/ahi.h>
#include <stdlib.h>
#include <string.h>

struct Library *AHIBase;
static volatile ULONG frames, calls, repeats;
static WORD last[8192];
static ULONG lastn;
static volatile double sumsq;

AROS_UFH3(IPTR, rec, AROS_UFHA(struct Hook *, h, A0), AROS_UFHA(struct AHIAudioCtrl *, a, A2),
          AROS_UFHA(struct AHIRecordMessage *, m, A1))
{
    AROS_USERFUNC_INIT
    const WORD *s = m->ahirm_Buffer;
    ULONG i, n = m->ahirm_Length * (m->ahirm_Type == AHIST_S16S ? 2 : 1);
    (void)h; (void)a;
    for (i = 0; i < n; i++) sumsq += (double)s[i] * s[i];
    if (n && n <= 8192) {
        if (n == lastn && !memcmp(s, last, n * sizeof(WORD))) repeats++;
        memcpy(last, s, n * sizeof(WORD));
        lastn = n;
    }
    frames += m->ahirm_Length;
    calls++;
    return 0;
    AROS_USERFUNC_EXIT
}

int main(int argc, char **argv)
{
    struct MsgPort *port = CreateMsgPort();
    struct AHIRequest *req = port ? (struct AHIRequest *)CreateIORequest(port, sizeof(*req)) : NULL;
    struct Hook hook;
    struct AHIAudioCtrl *ctrl;
    ULONG id = argc > 1 ? strtoul(argv[1], NULL, 16) : 0x003B1001, secs = argc > 2 ? atoi(argv[2]) : 5, t, freq = 0;
    if (!req) return 20;
    req->ahir_Version = 4;
    if (OpenDevice((CONST_STRPTR)AHINAME, AHI_NO_UNIT, (struct IORequest *)req, 0)) return 20;
    AHIBase = (struct Library *)req->ahir_Std.io_Device;
    hook.h_Entry = (HOOKFUNC)(void (*)(void))rec;
    ctrl = AHI_AllocAudio(AHIA_AudioID, id, AHIA_MixFreq, 48000, AHIA_Channels, 1, AHIA_Sounds, 1,
                          AHIA_RecordFunc, (IPTR)&hook, TAG_DONE);
    if (!ctrl) { PutStr((CONST_STRPTR)"AllocAudio failed\n"); return 10; }
    AHI_ControlAudio(ctrl, AHIC_MixFreq_Query, (IPTR)&freq, TAG_DONE);
    Printf((CONST_STRPTR)"mode %08lx at %ld Hz: record %ld\n", (unsigned long)id, (long)freq,
           (long)AHI_ControlAudio(ctrl, AHIC_Record, TRUE, TAG_DONE));
    for (t = 0; t < secs; t++) {
        ULONG f0 = frames, c0 = calls, r0 = repeats;
        double s0 = sumsq;
        Delay(50);
        Printf((CONST_STRPTR)"%2ld s: %6ld frames in %3ld blocks, rms %ld, repeated blocks %ld\n", (long)t + 1,
               (long)(frames - f0), (long)(calls - c0),
               frames > f0 ? (long)__builtin_sqrt((sumsq - s0) / ((frames - f0) * 2.0)) : 0L, (long)(repeats - r0));
    }
    AHI_ControlAudio(ctrl, AHIC_Record, FALSE, TAG_DONE);
    AHI_FreeAudio(ctrl);
    CloseDevice((struct IORequest *)req);
    DeleteIORequest((struct IORequest *)req);
    DeleteMsgPort(port);
    return 0;
}
