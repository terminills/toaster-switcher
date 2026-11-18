/* The Switcher's plug-ins (tsplugin.h): the libraries in Toaster:Plugins and what they offer. */
#ifndef PLUGINS_H
#define PLUGINS_H
#include "tsplugin.h"

#define MAXPLUGINS 16

struct plugin_info {
    char file[40];              /* "bars.tsplugin" */
    char name[40], about[96];
    int version, revision;
    int ninputs, noutputs;      /* kinds of streams and of outputs it has */
    int uses;                   /* streams and outputs open through it now */
};

/* Before anything else here. plugins_exit closes them all, once everything that held one has ended. */
void plugins_init(void);
void plugins_exit(void);
/* Read the drawer again. The plug-ins nothing uses are closed first, so a file that was replaced is loaded anew
 * and one that was taken away is gone; one in use stays as it is. Returns how many there are. From the task that
 * called plugins_init only: it is the one the libraries are opened and closed by. */
int plugins_scan(void);
int plugins_info(int index, struct plugin_info *out);   /* 0 for one that is there */

/* The index-th kind of stream and of output of all plug-ins, NULL past the last. For the task that scans: they
 * are the plug-in's own memory, good until its next plugins_scan. */
const struct tsp_input *plugins_input_kind(int index);
const struct tsp_output *plugins_output_kind(int index);

/* From any thread: the backend of that name, and with it its plug-in held in memory until the backend is released.
 * NULL when no plug-in has one. Releasing a backend that is no plug-in's does nothing. */
const struct tsi_backend *plugins_hold_input(const char *name);
const struct tso_backend *plugins_hold_output(const char *name);
void plugins_release_input(const struct tsi_backend *be);
void plugins_release_output(const struct tso_backend *be);
#endif
