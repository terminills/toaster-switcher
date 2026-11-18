/* The Inputs and Outputs window: which stream each switcher input shows, where Program goes besides the monitor,
 * and the plug-ins that bring further kinds of both. */
#ifndef INPUTSWIN_H
#define INPUTSWIN_H
#include <exec/types.h>
#include "inputs.h"
#include "outputs.h"

struct Screen;
struct colorfont;
struct inputswin;

/* Drivers (backend:arg, e.g. "vhi:uvc") the window offers sources from; kept with the input settings. */
#define MAXDRIVERS 8
#define NICKLEN 24
struct inputprefs {
    char driver[MAXDRIVERS][SPECLEN];
    int ndrivers;
    char spec[NSLOTS][SPECLEN];
    int has[NSLOTS];            /* spec[] set for this slot */
    char nick[NSLOTS][NICKLEN]; /* the user's name for each input ("Cam 1", "DUT"), "" for the stream's own */
    int sound[NSLOTS];          /* SOUND_FOLLOW (0, the default), SOUND_ALWAYS, SOUND_OFF */
    char out[MAXOUTPUTS][SPECLEN];  /* the outputs, "backend:arg" ("net:8090"), "" for none */
    int outon[MAXOUTPUTS];      /* on: it runs, and starts with the Switcher */
};

/* What the user calls slot s: its nickname, else the stream's name, else "". */
const char *inputprefs_label(const struct inputprefs *p, struct inputs *in, int s);

void inputprefs_default(struct inputprefs *p);
int inputprefs_load(struct inputprefs *p, const char *file);      /* ENV:VideoToaster/Inputs */
int inputprefs_save(const struct inputprefs *p, const char *file);
/* Switch on the outputs that are on in p. */
void inputprefs_start_outputs(const struct inputprefs *p);

/* Open (or bring to front) the window with slot selected (-1: as it is). It is lettered in font, NewTek's interface
 * font, or with NULL in the screen's, and drawn scale times as large as the Toaster's panel is. */
struct inputswin *inputswin_open(struct inputswin *iw, struct Screen *scr, struct inputs *in, struct inputprefs *p,
                                 int slot, const struct colorfont *font, int scale);
ULONG inputswin_sigmask(struct inputswin *iw);
/* Now and then while it is open: the outputs' states change by themselves. */
void inputswin_poll(struct inputswin *iw);
/* Handle the window's messages; returns 0 once it closed (iw is freed then). *changed is set when an input changed. */
int inputswin_handle(struct inputswin *iw, int *changed);
void inputswin_close(struct inputswin *iw);
#endif
