/* Pages of the Toaster's character generator, as croutons (FORM CrUD of TYPE KEYF, FRAM, SCRO or CRAW) and as
 * framestore pictures. The picture at the head of the file is the page. ToasterCG for AROS writes a deep ILBM: a
 * frame in 24 planes, a key in 32 with its alpha in the last eight, a scroll or crawl as the strip of its lines
 * (32 planes; BMHD's y is the line a crawl sits at). NewTek's CG wrote the framestore's own planes (16, or 18 with
 * a 2-bit key, mapped by a PLTP chunk); those are read for frames and keys. Its scrolls and crawls, a sheet of
 * glyphs moved by code that was not released, are not. */
#ifndef CGPAGE_INCLUDED
#define CGPAGE_INCLUDED
#include <stdint.h>
#include <stddef.h>

#define CGPAGE_W 752            /* the CG's frame */
#define CGPAGE_H 480
#define CGPAGE_SAFE_BOTTOM 432  /* where a frozen scroll's last line stops */

enum { CGP_STILL, CGP_SCROLL, CGP_CRAWL };
enum { CGP_ONCE, CGP_FREEZE, CGP_LOOP };

struct cgpage {
    int w, h;                   /* the picture: the frame, or a scroll's or crawl's strip */
    int top;                    /* the frame line a crawl's strip starts at */
    uint8_t *rgba;              /* w x h: red, green, blue, alpha (straight) */
    int kind, mode;             /* CGP_ */
    int speed2;                 /* half pixels a field */
    int ow, oh, sw, sh;         /* the picture scaled for an ow x oh output: sw x sh, premultiplied */
    uint8_t *scaled;
};

/* Decode the first picture of a page file or a framestore file (kind and speed are the caller's). 0 on success. */
int cgpage_decode(struct cgpage *p, const uint8_t *file, size_t len);
void cgpage_free(struct cgpage *p);
/* Half pixels a field for a scroll's speed (0..8) or a crawl's (0..3): the Toaster's steps, a scroll half a line
 * and then 1 to 8 lines a field, a crawl 2, 4, 8 or 16 pixels. */
int cgpage_speed(int kind, int speed);
/* Where a moving page is, fields after it went to air: *pos2 in half pixels of the frame, the strip's start
 * against the frame's (negative while it comes in). 0 once a page played once is over; a frozen scroll stays at
 * its stop and a looping page starts again. */
int cgpage_at(const struct cgpage *p, long fields, long *pos2);
/* The page at pos2 over buf (ow x oh, 0x00RRGGBB) at level of max. */
void cgpage_blend(struct cgpage *p, long pos2, uint32_t *buf, int ow, int oh, int level, int max);
/* The page over black as an ow x oh frame, 0x00RRGGBB in a new buffer (free() it), NULL without memory. */
uint32_t *cgpage_frame(struct cgpage *p, int ow, int oh);
/* The picture itself over black, w x h 0x00RRGGBB in a new buffer (free() it), NULL without memory. */
uint32_t *cgpage_rgb(const struct cgpage *p);
#endif
