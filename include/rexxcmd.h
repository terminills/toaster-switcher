/* The Switcher's ARexx function, Switcher(<command>[, <arguments>]) on ToasterARexx.port, as the Video Toaster 4.2
 * documents it (switcherarexx.doc) and its Switcher.main answers it: "OK" or a value on success, error 10 on
 * failure. A command works out what is to happen; what touches the window or runs over time is left to the caller. */
#ifndef REXXCMD_H_INCLUDED
#define REXXCMD_H_INCLUDED
#include <stdint.h>
#include "switcher.h"
#include "framestore.h"
#include "inputs.h"

struct rxstate {
    struct switcher *sw;
    struct framestore *fs;
    struct inputs *in;
    uint32_t *dvbuf;            /* room for one frame of DV1 */
    int mix, mixmax;            /* Program mix: 0 = Main, mixmax = Preview */
    int tbdown;                 /* the T-bar's next travel is downwards */
    long now;                   /* in 1/50 s */
    long *framebase;            /* WAIT counts frames from here */
    const int *speedticks;      /* AUTO length for S, M and F */
};

enum {
    RXA_NONE,
    RXA_PRESS,                  /* the button row, index */
    RXA_TBAR,                   /* the T-bar to the mix value */
    RXA_UATO,                   /* the transition back to its start */
    RXA_TOSW,                   /* the Toaster to the front */
    RXA_TOWB,                   /* the Workbench to the front */
    RXA_CHANGED,                /* settings were changed: show them */
    RXA_QUIT
};

struct rxout {
    long rc, rc2;               /* 0, or 10 (5 and 1 when the function is not Switcher) */
    char text[128];             /* the result of a call that succeeded */
    int act, row, index, value;
    long until;                 /* answer no sooner than this */
};

void rxswitcher(const struct rxstate *st, const char *fn, int argc, const char *const *argv, struct rxout *o);
#endif
