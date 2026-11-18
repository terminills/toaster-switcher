/* The library a Toaster plug-in is on disk (see tsplugin.h): linked in front of the plug-in's own source. It takes
 * the Switcher's C library for the plug-in, hands out the plug-in's description, and leaves memory with its last
 * close. */
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/resident.h>
#include <dos/dos.h>
#include <aros/libcall.h>
#include <aros/asmcall.h>
#include <aros/symbolsets.h>
#include <proto/exec.h>
#include "tsplugin.h"

struct ExecBase *SysBase = NULL;
static BPTR seglist;
static int started;

/* What the C library's functions are called through. They are the Switcher's own, set when it asks for the
 * description: a C library opened here would be another program's in the Switcher's task, and posixc.library, which
 * a task has once, would be left calling a stdc.library that goes with the plug-in. Being defined here they also
 * keep the linker libraries from opening theirs (the Makefile checks that); they are given a value because a
 * compiler that makes commons of the others (gcc before 10) would have the linker take the libraries' after all. */
struct StdCBase *StdCBase = NULL;
struct StdCIOBase *StdCIOBase = NULL;
struct PosixCBase *PosixCBase = NULL;

extern const char tsp_end[];
static const APTR functable[];
static const IPTR inittable[4];

/* run as a command it does nothing */
__startup LONG tsp_entry(void)
{
    return -1;
}

__section(".text.romtag") const struct Resident tsp_romtag = {
    RTC_MATCHWORD,
    (struct Resident *)&tsp_romtag,
    (APTR)&tsp_end,
    RTF_AUTOINIT,
    0,                                  /* the version is the plug-in's own, set when the library is made */
    NT_LIBRARY,
    0,
    (CONST_STRPTR)tsp_libname,
    (CONST_STRPTR)&tsp_idstring[6],
    (APTR)inittable,
    0,
    NULL
};

THIS_PROGRAM_HANDLES_SYMBOLSET(INIT)
THIS_PROGRAM_HANDLES_SYMBOLSET(EXIT)
DECLARESET(INIT)
DECLARESET(EXIT)
THIS_PROGRAM_HANDLES_SYMBOLSET(PROGRAM_ENTRIES)
DECLARESET(PROGRAM_ENTRIES)
THIS_PROGRAM_HANDLES_SYMBOLSET(CTORS)
THIS_PROGRAM_HANDLES_SYMBOLSET(DTORS)
DECLARESET(CTORS)
DECLARESET(DTORS)
THIS_PROGRAM_HANDLES_SYMBOLSET(INIT_ARRAY)
THIS_PROGRAM_HANDLES_SYMBOLSET(FINI_ARRAY)
DECLARESET(INIT_ARRAY)
DECLARESET(FINI_ARRAY)
THIS_PROGRAM_HANDLES_SYMBOLSET(LIBS)
DECLARESET(LIBS)

static void freebase(struct Library *base)
{
    FreeMem((UBYTE *)base - base->lib_NegSize, base->lib_NegSize + base->lib_PosSize);
}

static void leave(void)
{
    if (started) {
        set_call_funcs(SETNAME(FINI_ARRAY), -1, 0);
        set_call_funcs(SETNAME(DTORS), 1, 0);
        set_call_funcs(SETNAME(EXIT), -1, 0);
    }
    set_close_libraries();
}

AROS_UFH3(static struct Library *, tsp_init,
    AROS_UFHA(struct Library *, base, D0),
    AROS_UFHA(BPTR, segs, A0),
    AROS_UFHA(struct ExecBase *, sysbase, A6))
{
    AROS_USERFUNC_INIT
    SysBase = sysbase;
    seglist = segs;
    base->lib_Version = tsp_version;
    base->lib_Revision = tsp_revision;
    if (set_open_libraries()) return base;
    set_close_libraries();
    freebase(base);
    return NULL;
    AROS_USERFUNC_EXIT
}

AROS_LH1(static struct Library *, tsp_open,
    AROS_LHA(ULONG, version, D0),
    struct Library *, base, 1, TSPlugin)
{
    AROS_LIBFUNC_INIT
    (void)version;
    base->lib_OpenCnt++;
    base->lib_Flags &= ~LIBF_DELEXP;
    return base;
    AROS_LIBFUNC_EXIT
}

AROS_LH0(static BPTR, tsp_expunge,
    struct Library *, base, 3, TSPlugin)
{
    AROS_LIBFUNC_INIT
    if (base->lib_OpenCnt) {
        base->lib_Flags |= LIBF_DELEXP;
        return BNULL;
    }
    Remove(&base->lib_Node);
    leave();
    freebase(base);
    return seglist;
    AROS_LIBFUNC_EXIT
}

AROS_LH0(static BPTR, tsp_close,
    struct Library *, base, 2, TSPlugin)
{
    AROS_LIBFUNC_INIT
    if (--base->lib_OpenCnt) return BNULL;
    return AROS_LC0(BPTR, tsp_expunge, struct Library *, base, 3, TSPlugin);
    AROS_LIBFUNC_EXIT
}

AROS_LH0(static APTR, tsp_null,
    struct Library *, base, 4, TSPlugin)
{
    AROS_LIBFUNC_INIT
    (void)base;
    return NULL;
    AROS_LIBFUNC_EXIT
}

/* The plug-in's own start (constructors and the like) waits for the first call: it may use the C library. */
AROS_LH1(static const struct tsp_plugin *, tsp_describe,
    AROS_LHA(const struct tsp_host *, host, A0),
    struct Library *, base, TSP_DESCRIBE_LVO, TSPlugin)
{
    AROS_LIBFUNC_INIT
    (void)base;
    if (!host || host->api_version != TSP_API_VERSION || !host->stdc || !host->stdcio || !host->posixc) return NULL;
    if (!started) {
        StdCBase = host->stdc;
        StdCIOBase = host->stdcio;
        PosixCBase = host->posixc;
        if (!set_call_funcs(SETNAME(INIT), 1, 1)) {
            set_call_funcs(SETNAME(EXIT), -1, 0);
            return NULL;
        }
        set_call_funcs(SETNAME(CTORS), -1, 0);
        set_call_funcs(SETNAME(INIT_ARRAY), 1, 0);
        started = 1;
    }
    return &tsp_plugin;
    AROS_LIBFUNC_EXIT
}

DEFINESET(INIT)
DEFINESET(EXIT)
DEFINESET(CTORS)
DEFINESET(DTORS)
DEFINESET(INIT_ARRAY)
DEFINESET(FINI_ARRAY)

static const APTR functable[] = {
    &AROS_SLIB_ENTRY(tsp_open, TSPlugin, 1),
    &AROS_SLIB_ENTRY(tsp_close, TSPlugin, 2),
    &AROS_SLIB_ENTRY(tsp_expunge, TSPlugin, 3),
    &AROS_SLIB_ENTRY(tsp_null, TSPlugin, 4),
    &AROS_SLIB_ENTRY(tsp_describe, TSPlugin, TSP_DESCRIBE_LVO),
    (void *)-1
};

static const IPTR inittable[4] = {
    sizeof(struct Library),
    (IPTR)functable,
    0,
    (IPTR)tsp_init
};
