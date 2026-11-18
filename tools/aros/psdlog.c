/* psdlog: print Poseidon's message log without flushing it. C:PsdErrorlog reads the level through a ULONG where
 * psdGetAttrs() stores an IPTR, which faults on 64-bit targets. */
#include <exec/types.h>
#include <libraries/poseidon.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/poseidon.h>

struct Library *ps;

int main(void)
{
    struct List *msgs = NULL;
    struct Node *pem;
    if (!(ps = OpenLibrary((CONST_STRPTR)"poseidon.library", 4))) return 20;
    psdLockReadPBase();
    psdGetAttrs(PGA_STACK, NULL, PA_ErrorMsgList, (IPTR)&msgs, TAG_END);
    if (msgs)
        for (pem = msgs->lh_Head; pem->ln_Succ; pem = pem->ln_Succ) {
            IPTR level = 0, origin = 0, msg = 0;
            psdGetAttrs(PGA_ERRORMSG, pem, EMA_Level, (IPTR)&level, EMA_Origin, (IPTR)&origin, EMA_Msg, (IPTR)&msg, TAG_END);
            Printf((CONST_STRPTR)"psdlog: %2ld-%s: %s\n", level, origin ? origin : (IPTR)"?", msg ? msg : (IPTR)"?");
        }
    psdUnlockPBase();
    CloseLibrary(ps);
    return 0;
}
