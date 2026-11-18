/* uvcgrab [SECS]: grab from the first camera of uvc.class through the class's own methods (UVCM_COUNT, UVCM_INFO,
 * UVCM_GRAB), for a uvc.class that publishes no VHI library. Reports the bound cameras and the grabs that worked. */
#include <exec/types.h>
#include <exec/execbase.h>
#include <aros/libcall.h>
#include <devices/uvc_capture.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>

static IPTR method(struct Library *base, ULONG id, IPTR *data)
{
    return AROS_LC2(IPTR, usbDoMethodA, AROS_LCA(ULONG, id, D0), AROS_LCA(IPTR *, data, A1), struct Library *, base, 7, nep);
}

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 5;
    struct Library *base = OpenLibrary((CONST_STRPTR)"uvc.class", 0);
    struct UVCCameraInfo info;
    struct DateStamp start, now;
    IPTR args[4], count;
    ULONG ok = 0, failed = 0, cap;
    UBYTE *buf;
    if (!base) { printf("uvcgrab: uvc.class is not in memory\n"); return 20; }
    count = method(base, UVCM_COUNT, args);
    printf("uvcgrab: %lu cameras bound\n", (unsigned long)count);
    args[0] = 0; args[1] = (IPTR)&info;
    if (!count || !method(base, UVCM_INFO, args)) { CloseLibrary(base); return 5; }
    printf("uvcgrab: camera 0 '%s' %lux%lu\n", info.name, (unsigned long)info.width, (unsigned long)info.height);
    cap = info.width * info.height * 3;
    if (!(buf = AllocVec(cap, MEMF_ANY))) { CloseLibrary(base); return 20; }
    DateStamp(&start);
    do {
        args[0] = info.id; args[1] = (IPTR)buf; args[2] = cap;
        if (method(base, UVCM_GRAB, args)) ok++; else failed++;
        DateStamp(&now);
    } while ((now.ds_Minute - start.ds_Minute) * 3000 + now.ds_Tick - start.ds_Tick < secs * 50);
    printf("uvcgrab: %lu pictures, %lu failed in %d s\n", (unsigned long)ok, (unsigned long)failed, secs);
    FreeVec(buf);
    CloseLibrary(base);
    return 0;
}
