/* psdtouch: change Poseidon's config without any uvc.class code, to see what a config change alone does to the guest.
 * Poseidon answers a changed config by sending UCM_ConfigChangedEvent to every class about a second later.
 *   psdtouch             store a new value in a config entry of its own (owner psdtouch.test), print it
 *   psdtouch TASKS       every task's state from task.resource; an entry whose name pointer is not an address is
 *                        reported as BAD ENTRY instead of being read
 *   psdtouch WHERE a...  the module and symbol each hex address belongs to, from debug.library */
#include <exec/types.h>
#include <exec/execbase.h>
#include <libraries/poseidon.h>
#include <libraries/debug.h>
#include <proto/debug.h>
#include <proto/exec.h>
#include <proto/poseidon.h>
#include <proto/task.h>
#include <resources/task.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Library *ps, *DebugBase;
APTR TaskResBase;

static char out[16384];
static int used;
#define OUT(...) do { if (used < (int)sizeof(out) - 400) used += snprintf(out + used, 400, __VA_ARGS__); } while (0)

static int tasks(void)
{
    static const char *const states[] = { "invalid", "added", "run", "ready", "wait", "except", "removed", "tombstoned", "spin" };
    struct TaskList *tl;
    struct Task *t;
    int n = 0, bad = 0;

    if (!(TaskResBase = OpenResource((CONST_STRPTR)"task.resource"))) return 10;
    tl = LockTaskList(LTF_ALL);
    while ((t = NextTaskEntry(tl, LTF_ALL))) {
        IPTR name = (IPTR)t->tc_Node.ln_Name;
        BOOL sane = name >= 0x1000 && name < 0x0000800000000000ULL && TypeOfMem((APTR)name) &&
                    t->tc_State >= TS_ADDED && t->tc_State <= TS_EXCEPT;
        char copy[49];
        int i;

        for (i = 0; sane && i < 48 && ((char *)name)[i] >= ' ' && ((char *)name)[i] <= '~'; i++) copy[i] = ((char *)name)[i];
        copy[sane ? i : 0] = 0;
        OUT("psdtouch: task %p %-10s pri %4d sigwait %08lx %s", t, t->tc_State < 9 ? states[t->tc_State] : "?",
            (int)t->tc_Node.ln_Pri, (unsigned long)t->tc_SigWait, copy);
        if (!sane) {
            OUT("BAD ENTRY: type %d state %d name pointer %p", (int)t->tc_Node.ln_Type, (int)t->tc_State, (APTR)name);
            bad++;
        }
        OUT("\n");
        n++;
    }
    UnLockTaskList(tl, LTF_ALL);
    fputs(out, stdout);
    printf("psdtouch: %d tasks, %d bad\n", n, bad);
    return 0;
}

static int where(int argc, char **argv)
{
    int i;

    if (!(DebugBase = OpenLibrary((CONST_STRPTR)"debug.library", 0))) { printf("psdtouch: no debug.library\n"); return 10; }
    for (i = 2; i < argc; i++) {
        IPTR addr = strtoull(argv[i], NULL, 16);
        char *mod = NULL, *sym = NULL;
        APTR start = NULL, segstart = NULL;

        if (DecodeLocation((APTR)addr, DL_ModuleName, &mod, DL_SymbolName, &sym, DL_SymbolStart, &start,
                           DL_SegmentStart, &segstart, TAG_DONE))
            printf("psdtouch: where %016llx %s %s+0x%llx (segment+0x%llx)\n", (unsigned long long)addr, mod ? mod : "?",
                   sym ? sym : "?", (unsigned long long)(start ? addr - (IPTR)start : 0),
                   (unsigned long long)(segstart ? addr - (IPTR)segstart : 0));
        else
            printf("psdtouch: where %016llx not in a known module\n", (unsigned long long)addr);
    }
    CloseLibrary(DebugBase);
    return 0;
}

int main(int argc, char **argv)
{
    static char owner[] = "psdtouch.test", devid[] = "PsdTouch-0000-0000-00", ifid[] = "00-00-00-00-00";
    ULONG chunk[3] = { AROS_LONG2BE(IFFCHNK_UNIT), AROS_LONG2BE(sizeof(ULONG)), 0 }, *old;
    APTR pic;

    if (argc > 1 && !strcmp(argv[1], "TASKS")) return tasks();
    if (argc > 2 && !strcmp(argv[1], "WHERE")) return where(argc, argv);
    if (!(ps = OpenLibrary((CONST_STRPTR)"poseidon.library", 4))) { printf("psdtouch: no poseidon.library\n"); return 10; }
    if (!(pic = psdGetUsbDevCfg((STRPTR)owner, (STRPTR)devid, (STRPTR)ifid))) {
        psdSetUsbDevCfg((STRPTR)owner, (STRPTR)devid, (STRPTR)ifid, NULL);
        pic = psdGetUsbDevCfg((STRPTR)owner, (STRPTR)devid, (STRPTR)ifid);
    }
    if (pic && (old = psdGetCfgChunk(pic, IFFCHNK_UNIT))) { chunk[2] = old[2] + 1; psdFreeVec(old); }
    printf("psdtouch: %s value %lu\n", pic && psdAddCfgEntry(pic, chunk) ? "stored" : "NOT STORED", (unsigned long)chunk[2]);
    CloseLibrary(ps);
    return 0;
}
