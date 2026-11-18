/* ahimodes: list AHI's audio modes with their record / full-duplex flags (to find a USB device's recording mode). */
#include <exec/memory.h>
#include <devices/ahi.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/ahi.h>

struct Library *AHIBase;

int main(void)
{
    struct MsgPort *port = CreateMsgPort();
    struct AHIRequest *req = port ? (struct AHIRequest *)CreateIORequest(port, sizeof(*req)) : NULL;
    ULONG id = AHI_INVALID_ID;
    if (!req) return 20;
    req->ahir_Version = 4;
    if (OpenDevice((CONST_STRPTR)AHINAME, AHI_NO_UNIT, (struct IORequest *)req, 0)) return 20;
    AHIBase = (struct Library *)req->ahir_Std.io_Device;
    while ((id = AHI_NextAudioID(id)) != AHI_INVALID_ID) {
        char name[128] = "";
        IPTR rec = 0, fd = 0, nf = 0;
        AHI_GetAudioAttrs(id, NULL, AHIDB_Name, (IPTR)name, AHIDB_BufferLen, sizeof(name), AHIDB_Record, (IPTR)&rec,
                          AHIDB_FullDuplex, (IPTR)&fd, AHIDB_Frequencies, (IPTR)&nf, TAG_DONE);
        Printf((CONST_STRPTR)"%08lx record %ld duplex %ld freqs %ld  %s\n", (unsigned long)id, (long)rec, (long)fd,
               (long)nf, name);
    }
    CloseDevice((struct IORequest *)req);
    DeleteIORequest((struct IORequest *)req);
    DeleteMsgPort(port);
    return 0;
}
