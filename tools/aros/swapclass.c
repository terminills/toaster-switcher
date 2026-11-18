/* swapclass NAME/A FILE/A: replace a loaded Poseidon USB class (e.g. uvc.class) by another build of it, for testing
 * a class without rebuilding the system image. Removes the class (its bindings go), adds FILE, rescans. */
#include <exec/types.h>
#include <libraries/poseidon.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/poseidon.h>
#include <string.h>

struct Library *ps;

int main(int argc, char **argv)
{
    struct List *classes = NULL;
    struct Node *puc;
    APTR found = NULL, added;
    if (argc < 3) { PutStr((CONST_STRPTR)"usage: swapclass name file\n"); return 20; }
    if (!(ps = OpenLibrary((CONST_STRPTR)"poseidon.library", 4))) return 20;
    psdLockWritePBase();
    psdGetAttrs(PGA_STACK, NULL, PA_ClassList, (IPTR)&classes, TAG_END);
    if (classes)
        for (puc = classes->lh_Head; puc->ln_Succ; puc = puc->ln_Succ) {
            STRPTR name = NULL;
            psdGetAttrs(PGA_USBCLASS, puc, UCA_ClassName, (IPTR)&name, TAG_END);
            if (name && !strcmp((char *)name, argv[1])) { found = puc; break; }
        }
    psdUnlockPBase();
    if (found) {
        struct Library *lib;
        psdRemClass(found);
        /* a closed library stays in memory until expunged, and OpenLibrary by name would find it again */
        Forbid();
        lib = (struct Library *)FindName(&SysBase->LibList, (CONST_STRPTR)argv[1]);
        if (lib && !lib->lib_OpenCnt) RemLibrary(lib);
        Permit();
        lib = (struct Library *)FindName(&SysBase->LibList, (CONST_STRPTR)argv[1]);
        Printf((CONST_STRPTR)"removed %s%s\n", (IPTR)argv[1], (IPTR)(lib ? " (still in memory: in use)" : ", expunged"));
    } else {
        Printf((CONST_STRPTR)"%s was not loaded\n", (IPTR)argv[1]);
    }
    added = psdAddClass((STRPTR)argv[2], 0);
    Printf((CONST_STRPTR)"added %s: %s\n", (IPTR)argv[2], (IPTR)(added ? "yes" : "FAILED"));
    psdClassScan();
    CloseLibrary(ps);
    return added ? 0 : 10;
}
