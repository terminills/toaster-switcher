/* A public ARexx port: it answers function calls (a script reaches it with addlib(port, 0)) and sends macros to the
 * ARexx server, whose answers come back to it. */
#ifndef REXXHOST_H_INCLUDED
#define REXXHOST_H_INCLUDED

#define RX_MAXARGS 15
struct rexxhost;
struct rexxcall {
    void *msg;                  /* the call's message until rexx_reply; NULL when there is none */
    const char *fn;             /* function name */
    int argc;
    const char *argv[RX_MAXARGS];
};

/* NULL when the port can't be made or the name is taken. */
struct rexxhost *rexx_open(const char *port);
unsigned long rexx_sigmask(const struct rexxhost *h);
/* The next function call into c (1), or 0. Answers of macros are taken in on the way. */
int rexx_next(struct rexxhost *h, struct rexxcall *c);
/* Answer c: rc 0 with a result string, or rc and the error code rc2. */
void rexx_reply(struct rexxcall *c, long rc, long rc2, const char *result);
/* Start a macro (a command line for the ARexx server), without a console. 1 when it was sent. */
int rexx_run(struct rexxhost *h, const char *cmd);
/* A command for another program's port, not waited for: its answer is taken in as a macro's is. 1 when it was sent. */
int rexx_send(struct rexxhost *h, const char *port, const char *cmd);
/* Macros sent and not answered yet. */
int rexx_macros(const struct rexxhost *h);
void rexx_close(struct rexxhost *h);
#endif
