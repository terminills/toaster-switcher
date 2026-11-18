/* A public ARexx port for function calls and macros (see rexxhost.h). */
#include <proto/exec.h>
#include <proto/rexxsyslib.h>
#include <exec/ports.h>
#include <rexx/storage.h>
#include <rexx/errors.h>
#include <stdlib.h>
#include <string.h>
#include "rexxhost.h"

struct rexxhost {
    struct MsgPort *port;
    char name[48];
    int macros;
};

struct rexxhost *rexx_open(const char *port)
{
    struct rexxhost *h = calloc(1, sizeof(*h));
    if (!h) return NULL;
    strncpy(h->name, port, sizeof(h->name) - 1);
    Forbid();
    if (!FindPort((CONST_STRPTR)h->name) && (h->port = CreateMsgPort())) {
        h->port->mp_Node.ln_Name = h->name;
        h->port->mp_Node.ln_Pri = 0;
        AddPort(h->port);
    }
    Permit();
    if (!h->port) {
        free(h);
        return NULL;
    }
    return h;
}

unsigned long rexx_sigmask(const struct rexxhost *h)
{
    return h ? 1UL << h->port->mp_SigBit : 0;
}

int rexx_macros(const struct rexxhost *h)
{
    return h ? h->macros : 0;
}

int rexx_next(struct rexxhost *h, struct rexxcall *c)
{
    struct RexxMsg *rm;
    int i;
    while (h && (rm = (struct RexxMsg *)GetMsg(h->port))) {
        if (rm->rm_Node.mn_Node.ln_Type == NT_REPLYMSG) {       /* a macro of ours has ended */
            DeleteArgstring((UBYTE *)rm->rm_Args[0]);
            DeleteRexxMsg(rm);
            h->macros--;
            continue;
        }
        if ((rm->rm_Action & RXCODEMASK) != RXFUNC || !rm->rm_Args[0]) {
            rm->rm_Result1 = RC_ERROR;
            rm->rm_Result2 = ERR10_010;
            ReplyMsg(&rm->rm_Node);
            continue;
        }
        memset(c, 0, sizeof(*c));
        c->msg = rm;
        c->fn = (const char *)rm->rm_Args[0];
        c->argc = rm->rm_Action & RXARGMASK;
        if (c->argc > RX_MAXARGS) c->argc = RX_MAXARGS;
        for (i = 0; i < c->argc; i++) c->argv[i] = rm->rm_Args[i + 1] ? (const char *)rm->rm_Args[i + 1] : "";
        return 1;
    }
    return 0;
}

void rexx_reply(struct rexxcall *c, long rc, long rc2, const char *result)
{
    struct RexxMsg *rm = c->msg;
    if (!rm) return;
    rm->rm_Result1 = rc;
    rm->rm_Result2 = rc ? rc2 : 0;
    if (!rc && result && (rm->rm_Action & RXFF_RESULT) &&
        !(rm->rm_Result2 = (IPTR)CreateArgstring((UBYTE *)result, strlen(result))))
        rm->rm_Result1 = RC_ERROR;
    ReplyMsg(&rm->rm_Node);
    c->msg = NULL;
}

int rexx_run(struct rexxhost *h, const char *cmd)
{
    struct MsgPort *rexx;
    struct RexxMsg *rm;
    if (!h || !(rm = CreateRexxMsg(h->port, (UBYTE *)"rexx", (UBYTE *)h->name))) return 0;
    rm->rm_Action = RXCOMM | RXFF_NOIO;
    if (!(rm->rm_Args[0] = (IPTR)CreateArgstring((UBYTE *)cmd, strlen(cmd)))) {
        DeleteRexxMsg(rm);
        return 0;
    }
    Forbid();
    if ((rexx = FindPort((CONST_STRPTR)"REXX"))) PutMsg(rexx, &rm->rm_Node);
    Permit();
    if (!rexx) {
        DeleteArgstring((UBYTE *)rm->rm_Args[0]);
        DeleteRexxMsg(rm);
        return 0;
    }
    h->macros++;
    return 1;
}

/* Off the public list first; then the macros still running are waited for, their calls to this port failing. */
void rexx_close(struct rexxhost *h)
{
    struct rexxcall c;
    if (!h) return;
    RemPort(h->port);
    for (;;) {
        while (rexx_next(h, &c)) rexx_reply(&c, RC_ERROR, ERR10_012, NULL);
        if (h->macros <= 0) break;
        WaitPort(h->port);
    }
    DeleteMsgPort(h->port);
    free(h);
}
