/* ToasterCG as the Switcher's CG (see cgprog.h). */
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <stdio.h>
#include <string.h>
#include "cgprog.h"

int cg_loaded(void)
{
    int there;
    Forbid();
    there = FindPort((CONST_STRPTR)CG_PORT) != NULL;
    Permit();
    return there;
}

int cg_find(const char *named, char *path, size_t size)
{
    /* not Toaster:Programs/CG: that is the Toaster's own, which ToasterCG draws its panel from */
    static const char *const where[] = { "PROGDIR:ToasterCG/CG", "Toaster:ToasterCG/CG", "Toaster:Programs/ToasterCG/CG" };
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR win = me->pr_WindowPtr;
    unsigned i, n = named ? 1 : sizeof(where) / sizeof(where[0]);
    BPTR l = BNULL;

    me->pr_WindowPtr = (APTR)-1;        /* a volume that is not there fails, it is not asked for */
    for (i = 0; i < n && !l; i++)
        if ((l = Lock((CONST_STRPTR)(named ? named : where[i]), SHARED_LOCK)))
            snprintf(path, size, "%s", named ? named : where[i]);
    me->pr_WindowPtr = win;
    if (!l) return 0;
    UnLock(l);
    return 1;
}

int cg_start(const char *path)
{
    char cmd[300];
    BPTR in = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE), out = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);

    snprintf(cmd, sizeof(cmd), "\"%s\"", path);
    if (!in || !out || SystemTags((CONST_STRPTR)cmd, SYS_Asynch, TRUE, SYS_Input, (IPTR)in, SYS_Output, (IPTR)out,
                                  TAG_DONE) == -1) {
        if (in) Close(in);
        if (out) Close(out);
        return 0;
    }
    return 1;
}
