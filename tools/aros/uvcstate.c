/* uvcstate: print what uvc.class holds, read from its base without taking its lock, so it still answers when the
 * class is stuck: who owns nh_Lock and who waits for it, the bindings, and the VHI library bases.
 *   uvcstate TASKS       the same, then every task's state from task.resource (names from the task node only: C:TaskList
 *                        reads the command name of other shells while they change it)
 *   uvcstate UNITS       the unit numbers Poseidon's config holds for uvc.class
 *   uvcstate SAVE file   write Poseidon's config to file (psdSaveCfgToDisk), with an entry for each loaded class as
 *                        Trident's Save writes them: a config loaded later removes the classes it does not name
 *   uvcstate LOAD file   load Poseidon's config from file (psdLoadCfgFromDisk), as PsdStackLoader does during boot
 * Built against the class's own uvc.h: the layout must be that of the class under test. */
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/semaphores.h>
#include <libraries/poseidon.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/poseidon.h>
#include <proto/task.h>
#include <resources/task.h>
#include <stdio.h>
#include <string.h>
#include "uvc.h"

struct Library *ps;
APTR TaskResBase;

/* Text is collected while task switching is off and printed afterwards: printing waits, which would let it back on. */
static char out[16384];
static int used;
#define OUT(...) do { if (used < (int)sizeof(out) - 400) used += snprintf(out + used, 400, __VA_ARGS__); } while (0)

static void tasks(void)
{
    static const char *const states[] = { "invalid", "added", "run", "ready", "wait", "except", "removed", "tombstoned", "spin" };
    struct TaskList *tl;
    struct Task *t;
    int n = 0;

    if (!(TaskResBase = OpenResource((CONST_STRPTR)"task.resource"))) return;
    used = 0;
    tl = LockTaskList(LTF_ALL);
    while ((t = NextTaskEntry(tl, LTF_ALL))) {
        /* A task listed here can have a name pointer that is not an address at all (seen twice on SMP; strlen() on it
         * is a privilege violation), so the name is only read when the pointer and the state look sane. */
        IPTR name = (IPTR)t->tc_Node.ln_Name;
        BOOL sane = name >= 0x1000 && name < 0x0000800000000000ULL && TypeOfMem((APTR)name) &&
                    t->tc_State >= TS_ADDED && t->tc_State <= TS_EXCEPT;
        char copy[49];
        int i;

        for (i = 0; sane && i < 48 && ((char *)name)[i] >= ' ' && ((char *)name)[i] <= '~'; i++) copy[i] = ((char *)name)[i];
        copy[sane ? i : 0] = 0;
        OUT("uvcstate: task %p %-10s pri %4d sigwait %08lx %s", t, t->tc_State < 9 ? states[t->tc_State] : "?",
            (int)t->tc_Node.ln_Pri, (unsigned long)t->tc_SigWait, copy);
        if (!sane) OUT("BAD ENTRY: type %d state %d name pointer %p", (int)t->tc_Node.ln_Type, (int)t->tc_State, (APTR)name);
        OUT("\n");
        n++;
    }
    UnLockTaskList(tl, LTF_ALL);
    fputs(out, stdout);
    printf("uvcstate: %d tasks\n", n);
}

static const char *taskname(struct Task *t) { return t && t->tc_Node.ln_Name ? (const char *)t->tc_Node.ln_Name : "?"; }

static int units(void)
{
    APTR dev, ifc, data;
    int count = 0;
    Forbid();
    for (dev = psdFindCfgForm(NULL, IFFFORM_DEVICECFG); dev; dev = psdNextCfgForm(dev))
        for (ifc = psdFindCfgForm(dev, IFFFORM_IFCFGDATA); ifc; ifc = psdNextCfgForm(ifc)) {
            ULONG *chunk;
            STRPTR devid, ifid;
            if (!psdMatchStringChunk(ifc, IFFCHNK_OWNER, (CONST_STRPTR)"uvc.class")) continue;
            if (!(data = psdFindCfgForm(ifc, IFFFORM_IFCLSDATA)) || !(chunk = psdGetCfgChunk(data, IFFCHNK_UNIT))) continue;
            devid = psdGetStringChunk(dev, IFFCHNK_DEVID);
            ifid = psdGetStringChunk(ifc, IFFCHNK_IFID);
            printf("uvcstate: config unit %lu device '%s' interface '%s'\n", (unsigned long)chunk[2],
                   devid ? (char *)devid : "?", ifid ? (char *)ifid : "?");
            count++;
            psdFreeVec(devid); psdFreeVec(ifid); psdFreeVec(chunk);
        }
    Permit();
    printf("uvcstate: %d units in Poseidon's config\n", count);
    return 0;
}

/* The class entries of Trident's InternalCreateConfig(). */
static int classentries(void)
{
    struct List *classes = NULL;
    struct Node *puc;
    APTR pic, sub;
    ULONG form[3];
    int count = 0;

    if (!(pic = psdFindCfgForm(NULL, IFFFORM_STACKCFG))) return 0;
    while ((sub = psdFindCfgForm(pic, IFFFORM_USBCLASS))) psdRemCfgForm(sub);
    psdLockReadPBase();
    psdGetAttrs(PGA_STACK, NULL, PA_ClassList, (IPTR)&classes, TAG_END);
    for (puc = classes ? classes->lh_Head : NULL; puc && puc->ln_Succ; puc = puc->ln_Succ) {
        STRPTR name;
        form[0] = AROS_LONG2BE(ID_FORM); form[1] = AROS_LONG2BE(4); form[2] = AROS_LONG2BE(IFFFORM_USBCLASS);
        if (!(sub = psdAddCfgEntry(pic, form))) continue;
        if ((name = psdCopyStrFmt((CONST_STRPTR)"USB/%s", puc->ln_Name))) {
            psdAddStringChunk(sub, IFFCHNK_NAME, name);
            psdFreeVec(name);
            count++;
        }
    }
    psdUnlockPBase();
    return count;
}

int main(int argc, char **argv)
{
    struct NepUVCBase *nh;
    struct NepUVCVHIBase *vb;
    struct NepClassUVC *nch;
    struct SemaphoreRequest *sr;
    struct List *classes = NULL;
    struct Node *puc;
    IPTR usecnt = 0;
    int cams = 0, bases = 0;

    if (!(ps = OpenLibrary((CONST_STRPTR)"poseidon.library", 4))) return 20;
    if (argc > 1 && !strcmp(argv[1], "UNITS")) { units(); CloseLibrary(ps); return 0; }
    if (argc > 2 && !strcmp(argv[1], "SAVE")) {
        printf("uvcstate: %d class entries written to the config\n", classentries());
        printf("uvcstate: saved config to %s: %s\n", argv[2], psdSaveCfgToDisk((STRPTR)argv[2], FALSE) ? "yes" : "FAILED");
        CloseLibrary(ps); return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "LOAD")) {
        printf("uvcstate: loaded config from %s: %s\n", argv[2], psdLoadCfgFromDisk((STRPTR)argv[2]) ? "yes" : "FAILED");
        CloseLibrary(ps); return 0;
    }
    psdLockReadPBase();
    psdGetAttrs(PGA_STACK, NULL, PA_ClassList, (IPTR)&classes, TAG_END);
    if (classes)
        for (puc = classes->lh_Head; puc->ln_Succ; puc = puc->ln_Succ) {
            STRPTR name = NULL;
            psdGetAttrs(PGA_USBCLASS, puc, UCA_ClassName, (IPTR)&name, TAG_END);
            if (name && !strcmp((char *)name, "uvc.class")) psdGetAttrs(PGA_USBCLASS, puc, UCA_UseCount, (IPTR)&usecnt, TAG_END);
        }
    psdUnlockPBase();
    Forbid();
    if (!(nh = (struct NepUVCBase *)FindName(&SysBase->LibList, (CONST_STRPTR)"uvc.class"))) {
        Permit();
        printf("uvcstate: uvc.class is not in memory\n");
        CloseLibrary(ps);
        return 5;
    }
    OUT("uvcstate: nh_Lock owner %s nest %d queue %d, class open count %d, Poseidon use count %lu\n",
           nh->nh_Lock.ss_Owner ? taskname(nh->nh_Lock.ss_Owner) : "none", (int)nh->nh_Lock.ss_NestCount,
           (int)nh->nh_Lock.ss_QueueCount, (int)nh->nh_Library.lib_OpenCnt, (unsigned long)usecnt);
    for (sr = (struct SemaphoreRequest *)nh->nh_Lock.ss_WaitQueue.mlh_Head; sr->sr_Link.mln_Succ;
         sr = (struct SemaphoreRequest *)sr->sr_Link.mln_Succ)
        OUT("uvcstate: waiting for nh_Lock: %s\n", taskname(sr->sr_Waiter));
    for (nch = (struct NepClassUVC *)nh->nh_Cameras.lh_Head; nch->ncu_Node.ln_Succ;
         nch = (struct NepClassUVC *)nch->ncu_Node.ln_Succ, cams++)
        OUT("uvcstate: binding '%s' stream %s library %s\n", nch->ncu_Name, nch->ncu_Stream ? "running" : "none",
               nch->ncu_VHIBase ? nch->ncu_VHIBase->nvb_LibName : "none");
    for (vb = nh->nh_VHIBases; vb; vb = vb->nvb_Next, bases++)
        OUT("uvcstate: base %s unit %lu listed %d open %d binding %s id '%s' device '%s' interface '%s'\n",
               vb->nvb_LibName, (unsigned long)vb->nvb_Unit, (int)vb->nvb_Listed, (int)vb->nvb_Library.lib_OpenCnt,
               vb->nvb_Binding ? "yes" : "no", (char *)vb->nvb_Library.lib_IdString, vb->nvb_IDString,
               (char *)vb->nvb_IfIDString);
    Permit();
    fputs(out, stdout);
    printf("uvcstate: %d bindings, %d bases\n", cams, bases);
    if (argc > 1 && !strcmp(argv[1], "TASKS")) tasks();
    CloseLibrary(ps);
    return 0;
}
