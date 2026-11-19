/* The Toaster's CG as a program of its own: ToasterCG for AROS, started and entered from the Switcher as the
 * Toaster's Switcher loaded and selected its CG. Its pages come back as croutons (cgpage.h). */
#ifndef CGPROG_H_INCLUDED
#define CGPROG_H_INCLUDED
#include <stddef.h>

#define CG_PORT   "CG_AREXX"
#define CG_SELECT "FGC_SELECT"          /* to its port: to the front */
#define CG_UNLOAD "EXIT"

/* Loaded while its port is there. */
int cg_loaded(void);
/* Its program in path: the one named, or the CG of a ToasterCG drawer beside the Switcher or in Toaster:.
 * 0 when there is none. */
int cg_find(const char *named, char *path, size_t size);
/* Start it on its own; 1 when it was started. */
int cg_start(const char *path);
#endif
