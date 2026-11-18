/* swrx: call one function of an ARexx function host from the shell, as a script would after addlib(port, 0)
 * (an RXFUNC message that asks for a result), and print the answer.
 *
 * swrx PORT FUNCTION [arg ...]      e.g.  swrx ToasterARexx.port SWITCHER STAT MAIN */
#include <exec/ports.h>
#include <rexx/storage.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/rexxsyslib.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    struct MsgPort *reply, *host;
    struct RexxMsg *rm;
    int i, rc = 20;

    if (argc < 3 || argc > 18) {
        printf("usage: swrx PORT FUNCTION [arg ...]\n");
        return 20;
    }
    if (!(reply = CreateMsgPort())) return 20;
    if (!(rm = CreateRexxMsg(reply, NULL, NULL))) {
        DeleteMsgPort(reply);
        return 20;
    }
    rm->rm_Action = RXFUNC | RXFF_RESULT | (argc - 3);
    for (i = 2; i < argc; i++) rm->rm_Args[i - 2] = (IPTR)CreateArgstring((UBYTE *)argv[i], strlen(argv[i]));
    Forbid();
    if ((host = FindPort((CONST_STRPTR)argv[1]))) PutMsg(host, &rm->rm_Node);
    Permit();
    if (host) {
        WaitPort(reply);
        GetMsg(reply);
        rc = (int)rm->rm_Result1;
        if (rc) {
            printf("%s rc %d %ld\n", argv[2], rc, (long)rm->rm_Result2);
        } else if (rm->rm_Result2) {
            printf("%s rc 0 \"%s\"\n", argv[2], (char *)rm->rm_Result2);
            DeleteArgstring((UBYTE *)rm->rm_Result2);
        } else {
            printf("%s rc 0\n", argv[2]);
        }
    } else {
        printf("swrx: no port %s\n", argv[1]);
    }
    for (i = 2; i < argc; i++) DeleteArgstring((UBYTE *)rm->rm_Args[i - 2]);
    DeleteRexxMsg(rm);
    DeleteMsgPort(reply);
    return rc;
}
