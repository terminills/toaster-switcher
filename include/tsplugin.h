/* A Toaster plug-in: a library in the drawer Toaster:Plugins that brings the Switcher kinds of streams for its
 * inputs (tsinput.h) and places for Program to go (tsoutput.h). The Switcher opens every "<name>.tsplugin" there when
 * it starts and when its Plug-ins list is scanned again, and closes one that nothing uses before it reads the drawer
 * again; a plug-in leaves memory when it is closed, so a new build of it takes its place while the Switcher runs.
 *
 * A plug-in is its own source, with one TSP_IDENTITY() and one tsp_plugin, linked behind plugins/tsplugin_lib.c (see
 * plugins/bars.c, and the Makefile for the two commands). The C library it calls is the Switcher's own, handed to
 * it with tsp_host, so memory, files and errno are the Switcher's and nothing of them is given back when the plug-in
 * leaves: what it allocates or opens it frees and closes itself. Its backends are called as tsinput.h and tsoutput.h say, from the
 * Switcher's threads, never from the task that opened the library: a library that wants its opener's task
 * (bsdsocket.library) is opened in a backend's open(), not when the plug-in starts. */
#ifndef TSPLUGIN_H
#define TSPLUGIN_H
#include "tsinput.h"
#include "tsoutput.h"

#define TSP_API_VERSION 2
#define TSP_DRAWER "Toaster:Plugins"
#define TSP_SUFFIX ".tsplugin"

struct tsp_input {
    const struct tsi_backend *backend;
    const char *label;                  /* what its streams are, for the user: "Colour bars" */
    /* What its argument is, asked for when streams of it are added ("Address of the camera"). NULL when it takes
     * none: its streams are then offered as soon as the plug-in is loaded, opened with "". */
    const char *ask;
};

struct tsp_output {
    const struct tso_backend *backend;
    const char *label;                  /* where Program goes with it: "Pictures over HTTP" */
    const char *ask;                    /* what its argument is ("Port"), NULL when it takes none */
    const char *def;                    /* the argument offered first, "" for none */
};

struct tsp_plugin {
    uint32_t api_version;               /* TSP_API_VERSION */
    const char *name;                   /* "Colour bars" */
    const char *about;                  /* a line on what it is for */
    const struct tsp_input *inputs;
    int ninputs;
    const struct tsp_output *outputs;
    int noutputs;
};

/* What a plug-in's source defines: the description, and with TSP_IDENTITY the file it is built as (the library's
 * name: a file of another name does not open), its version and the day of it. */
extern const struct tsp_plugin tsp_plugin;
extern const char tsp_libname[], tsp_idstring[];
extern const unsigned short tsp_version, tsp_revision;
#define TSP_IDENTITY(file, version, revision, date) \
    const char tsp_libname[] = file; \
    const char tsp_idstring[] = "$VER: " file " " #version "." #revision " (" date ")\r\n"; \
    const unsigned short tsp_version = version, tsp_revision = revision

/* What the Switcher is to a plug-in: the C library it runs on (its StdCBase, StdCIOBase and PosixCBase). */
struct tsp_host {
    uint32_t api_version;               /* TSP_API_VERSION */
    void *stdc, *stdcio, *posixc;
};

/* The library's one function, at this vector: the plug-in's description, NULL when it is not for this Switcher. */
#define TSP_DESCRIBE_LVO 5
#define TSP_DESCRIBE(base, host) \
    AROS_LC1(const struct tsp_plugin *, tsp_describe, AROS_LCA(const struct tsp_host *, (host), A0), \
        struct Library *, (base), TSP_DESCRIBE_LVO, TSPlugin)
#endif
