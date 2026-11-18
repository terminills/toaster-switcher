/* The tags of a crouton by name, as the Editor's ARexx functions take them: the names and numbers of the Video
 * Toaster 4.2 Editor (editorarexx.doc lists the names). A tag holds a number or a table of bytes; in a crouton or
 * a project it is 0x80000000 | number followed by the value, or 0x10000000 | number followed by the size and the
 * bytes. */
#ifndef CROUTONTAGS_H_INCLUDED
#define CROUTONTAGS_H_INCLUDED
#include <stdint.h>

#define TAG_VALUE(n) (0x80000000u | (uint32_t)(n))
#define TAG_TABLE(n) (0x10000000u | (uint32_t)(n))
#define TAGN_Delay 0x6b
#define TAGN_CommandLine 0x84

/* The tag's number, with *table 1 when it holds a table; 0 for a name that is none. Case does not count. */
int crouton_tag(const char *name, int *table);
#endif
