/* The project in the switcher: its file and its croutons in sw->fx (up to MAXFX), as the Editor and the ARexx
 * functions change them. ovl is the crouton keyed over Program, or -1; it follows croutons that move. */
#ifndef SWPROJECT_H_INCLUDED
#define SWPROJECT_H_INCLUDED
#include <exec/types.h>
#include <stddef.h>
#include <stdint.h>
#include "switcher.h"

#define MAXFX 256
#define FXCUR(sw) ((sw)->fxsel >= 0 && (sw)->fxsel < (sw)->nfx ? &(sw)->fx[(sw)->fxsel] : NULL)

extern char projpath[256];

/* A file's contents (the caller frees them), NULL when it can't be read. */
uint8_t *readfile(const char *name, LONG *len);
const char *base_name(const char *path);
/* Open the crouton at path into sw->fx[at] (later ones move up), with its project entry (a new one when NULL).
 * 0 on success. */
int add_crouton(struct switcher *sw, int at, const char *path, const uint8_t *entry, size_t entrylen, int *ovl);
/* The project file's croutons in place of the present ones; -1, and nothing changed, when it can't be read. */
int load_project(struct switcher *sw, const char *project);
int save_project_to(struct switcher *sw, const char *path);
void select_effect(struct switcher *sw, int i, int onair);
/* The selected overlay finished fading: off when it was on air, otherwise on air in place of the previous one. */
int overlay_done(struct switcher *sw, int onair);
/* Scroll the grid to the selected crouton. */
void show_selected(struct switcher *sw);
void delete_crouton(struct switcher *sw, int i, int *ovl);
void clone_crouton(struct switcher *sw, int i, int *ovl);
#endif
