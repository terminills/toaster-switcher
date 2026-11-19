/* The Editor's ARexx functions on PROJECT_REXX_PORT, as the Video Toaster 4.2 documents them (editorarexx.doc) and
 * its Editor answers them: a result or nothing on success, error 10 with 17 (arguments missing), 18 (a bad
 * argument) or 12 (it can't be done) on failure. What needs a Flyer can't be done, but for its recording functions:
 * they record Program into a file (record.h). A function changes the project
 * and asks its requesters itself; what runs over time is left to the caller. */
#ifndef REXXEDIT_H_INCLUDED
#define REXXEDIT_H_INCLUDED
#include "switcher.h"
#include "rexxhost.h"
struct Window;

struct rxedit {
    struct switcher *sw;
    int *ovl;                   /* the crouton keyed over Program, or -1 */
    struct Window *w;
    struct rexxhost *host;      /* the Editor's port: it starts the tools that are ARexx programs */
    const int *speedticks;      /* effect length for S, M and F */
    int hold;                   /* a sequence's hold after a crouton that names none, in 1/50 s */
};

enum {
    RXE_NONE,
    RXE_CHANGED,                /* the project or the selection changed: show it */
    RXE_PLAY,                   /* play the project; the answer waits for its end */
    RXE_STOP,
    RXE_FILEREQ,                /* answer at once: the script asks what came of it */
    RXE_RECORD                  /* a recording runs; the answer is rec_status() when it has ended */
};

struct rxeout {
    long rc, rc2;               /* 0, or 10 and the error (5 and 1 when the function is none of the Editor's) */
    int result;                 /* text is the result of a call that succeeded; without one it has none */
    char text[512];
    int act;
};

void rxeditor(struct rxedit *st, const char *fn, int argc, const char *const *argv, struct rxeout *o);
/* The file requester STARTFILEREQ asked for and has not been told of yet (1, with what it is to show), and what
 * came of it for QUERYFILEREQ: the path, or NULL when it was cancelled. */
int rxedit_filereq_wanted(const char **title, const char **dir, const char **file);
void rxedit_filereq_done(const char *path);

/* What ADDTOOL and ADDPROGRAM put behind the Tools and Programs buttons. */
enum { RXL_TOOLS, RXL_PROGRAMS };
int rxedit_list(int list, const char **names, int max);
/* Start entry i of those: an ARexx program, or a DOS command. */
void rxedit_start(struct rxedit *st, int list, int i);
#endif
