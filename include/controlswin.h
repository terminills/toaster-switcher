/* The Controls window: a crouton's own speed and how long it holds in a Play sequence. */
#ifndef CONTROLSWIN_H
#define CONTROLSWIN_H
struct Window;

/* Edit *speed (-1 = the panel's S/M/F, 0..2 = S M F) and *hold (1/50 s, -1 = the default dflt) of the crouton
 * called name, in a window over parent. Returns 1 when they were changed with OK. */
int controls_edit(struct Window *parent, const char *name, int *speed, int *hold, int dflt);
#endif
