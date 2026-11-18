/* Toaster croutons (effect files) and projects, read in NewTek's own formats.
 * An effect file is IFF: FORM ANIM (the wipe: 2-plane fields, 0 = matte? 1 = matte, 2 = video A, 3 = video B),
 * FORM ILBM (the 80x50 crouton icon) and FORM CrUD (TYPE ANIM/ALGO/ILBM/MAIN/CTRL/REXX, LIBS, TAGS). An ARexx
 * crouton (REXX) is a script, with icon and CrUD in its .i file. A page of the character generator (KEYF, FRAM,
 * SCRO, CRAW) starts with its picture instead, see cgpage.h. */
#ifndef EFFECT_H
#define EFFECT_H
#include <stdint.h>
#include <stddef.h>
#include "algofx.h"
#include "cgpage.h"

#define ICON_W 80
#define ICON_H 50

enum { FX_OTHER, FX_ANIM, FX_ALGO, FX_ILBM, FX_MAIN, FX_CTRL, FX_REXX, FX_KEYF, FX_FRAM, FX_SCRO, FX_CRAW };
enum { CTRL_BLANK = 0, CTRL_STOP = 2 };

struct effect {
    char name[64];              /* file name part of the path */
    char path[256];             /* as the project names it ("Toaster:Effects/4000fx/wipes/nova ring") */
    uint8_t *entry;             /* its project entry, kept to write back unchanged (freed by effect_close) */
    size_t entrylen;
    int ctl_speed;              /* Controls, kept in the entry's tags: speed 0..2 (S M F) or -1 for the panel's, */
    int ctl_hold;               /* and the hold after it in a Play sequence in 1/50 s, -1 for the default */
    int type;
    uint8_t *tags;              /* copy of the crouton's own tags (CrUD TAGS), freed by effect_close */
    size_t tagslen;
    uint32_t icon[ICON_W * ICON_H];     /* 0x00RRGGBB */
    int has_icon;
    int mainsrc, maincolor;     /* MAIN croutons: source bits (1 2 4 8 inputs 1-4, 0x10 0x20 DV1 DV2, 0x40 BKG; tag
                                 * 0x80000076); BKG colour 1..9 as the BKG popup (tag 0x80000023, also on overlays) */
    int ctrl;                   /* CTRL croutons (tag 0x80000052): CTRL_BLANK holds a sequence, CTRL_STOP stops it */
    int overlay;                /* keyed over Program rather than a transition (tag 0x80000011, or the Overlays
                                 * folder): frames hold palette values, see effect_overlay_px */
    int keyonly, keymax, tcol;  /* overlay keying: all-black palette = values are key levels 0..keymax (video share);
                                 * otherwise value tcol shows video (255 = outside a mask plane) */
    int algotype, algomode;     /* ALGO croutons: AlgoFX type (tag 0x80000063) and mode (0x80000007) */
    uint8_t *algoblob;          /* copy of the AlgoFX parameter block (tag 0x1000001c), freed by effect_close */
    size_t algobloblen;
    struct algofx *algo;        /* parsed on demand by effect_frames; NULL when not understood */
    int fcount[3];              /* S, M, F from the crouton's tags (0 = not given): frames (30/s) for ALGO effects,
                                 * fields per wipe frame for ANIM effects - read from the tags, not yet checked
                                 * against NewTek's code */
    struct cgpage *page;        /* CG pages: the picture, decoded on demand by effect_frames. A key, a scroll and a
                                 * crawl are overlays; a frame is a picture for the Framestore */
    const uint8_t *file;        /* the whole file, owned by the caller */
    size_t filelen;
    int w, h, nframes;          /* wipe frames, decoded on demand */
    int bpp;                    /* bits per stored pixel: 2, 4 or 8 (planes rounded up; 9-plane files keep 8) */
    int stride;                 /* bytes per stored row */
    uint8_t *masks;             /* nframes * h * stride, leftmost pixel in the top bits */
    uint8_t role[256];          /* what each value shows: FXR_A (outgoing), FXR_B (incoming), FXR_COLOR */
    uint32_t color[256];        /* the wipe's CMAP: colour of each value that is not video (the bear's black
                                 * silhouette, the wedding book's artwork) */
    int soft;                   /* CMAP is a grey ramp: values are blend levels between A and B (soft edges) */
    uint8_t level[256];         /* soft: share of B per value, 0..255 */
    int wipemap;                /* FX_ILBM picture effects (masks holds the picture, one byte per pixel):
                                 * PIC_MAP: a grey wipe map, a pixel turns to B as the transition passes its level;
                                 * PIC_SCROLLV/H: a border bar taller/wider than the frame scrolled through it -
                                 * 0 = A, 1 = B, other values the bar's CMAP colours */
};
enum { PIC_NONE, PIC_MAP, PIC_SCROLLV, PIC_SCROLLH };
#define FRAME_W 768
#define FRAME_H 482

/* Picture effect: which of A (FXR_A), B (FXR_B) or a colour (FXR_COLOR, *rgb set) frame pixel fx,fy shows at
 * transition t of tmax; for PIC_MAP *level is B's share and FXR_COLOR is not used. */
int effect_picture(const struct effect *e, int fx, int fy, int t, int tmax, int *level, uint32_t *rgb);

/* Share of B (0..255) for a wipe-map pixel of grey g at transition t of tmax. */
static inline int wipemap_level(int g, int t, int tmax)
{
    const int soft = 24;    /* width of the soft edge, in grey levels */
    int x = t * (256 + soft) / tmax - g;
    return x <= 0 ? 0 : x >= soft ? 255 : x * 255 / soft;
}

/* Value roles differ between effects (the bear runs 2 -> 3 with 1 as its silhouette, the borders 1 -> 2 with 3 as
 * the border line), so they are taken from the wipe itself: the first frame is all outgoing video, the last all
 * incoming; other values draw in their CMAP colour, or blend A and B when the CMAP is a grey ramp. */
enum { FXR_A, FXR_B, FXR_COLOR };

/* Value of frame f at x,y. */
static inline int effect_mask(const struct effect *e, int f, int x, int y)
{
    const uint8_t *row = e->masks + ((size_t)f * e->h + y) * e->stride;
    if (e->bpp == 8) return row[x];
    if (e->bpp == 4) return (row[x >> 1] >> (x & 1 ? 0 : 4)) & 15;
    return (row[x >> 2] >> (6 - 2 * (x & 3))) & 3;
}

/* Read type and icon; keeps a pointer to buf (the caller keeps it alive). 0 on success. */
int effect_open(struct effect *e, const char *name, const uint8_t *buf, size_t n);
/* A crouton is a data file plus its .i icon file; some keep their CrUD (type, tags) and icon only in the .i.
 * Fill in what the data file lacked from the .i file's contents. */
void effect_merge_icon(struct effect *e, const uint8_t *ibuf, size_t n);
/* A scroll or a crawl: a page that moves, and goes to air and off it without a fade. */
static inline int effect_moves(const struct effect *e) { return e->type == FX_SCRO || e->type == FX_CRAW; }
/* Decode the wipe frames (FX_ANIM, op 5 and op 7), or a CG page's picture. 0 on success. */
int effect_frames(struct effect *e);
void effect_free_frames(struct effect *e);
/* Overlay frame f at frame position fx,fy (0..FRAME_W/H): artwork opacity 0..255, its colour in *rgb. Key-level
 * overlays fill with matte: the BKG colour their tag 0x80000023 names (maincolor; binoculars black, save green). */
int effect_overlay_px(const struct effect *e, int f, int fx, int fy, uint32_t matte, uint32_t *rgb);
/* What the crouton's place says: in an Overlays folder it is an overlay. */
void effect_note_path(struct effect *e, const char *path);
/* MAIN crouton: the input it puts on Main (0..6 as the switcher rows), or -1. */
int effect_main_slot(const struct effect *e);
/* Free what effect_open/effect_merge_icon allocated (not the file buffer). */
void effect_close(struct effect *e);

/* Length of the effect at speed 0..2 in 1/50 s; dflt when the crouton gives none. */
int effect_ticks(const struct effect *e, int speed, int dflt);

/* Toaster 4.x project file: entry file names in order, and with off each entry's offset (its size long; the entry
 * is 4 + that size bytes). Returns the count (<= max), -1 if not a project. */
int project_entries(const uint8_t *buf, size_t n, char names[][256], size_t off[], int max);
/* Length of the project's header (up to the first entry), 0 if not a project. */
size_t project_header(const uint8_t *buf, size_t n);
/* A new entry for a crouton at path, ObjectType type ("ANIM", "ALGO"...): {size, type, version 0, 4 pads, name
 * size, name, no tags}, as the Toaster loads them. */
uint8_t *project_entry_new(const char *path, const char *type, size_t *len);
/* Read the Controls (ctl_speed, ctl_hold) from e->entry's tags, -1 where it has none, and what else the switcher
 * takes from a tag: the project's go before the crouton's own. */
void effect_entry_controls(struct effect *e);
/* A tag of the crouton (see croutontags.h for the ids): the project's, and where it has none the crouton's own.
 * 1 when there is one: a number in *v, or a table's size in *v and its bytes at *data. */
int effect_tag(const struct effect *e, uint32_t id, uint32_t *v, const uint8_t **data);
/* Put a tag into e->entry, the number v or a table of v bytes at data; other tags are kept. 0 on success. */
int effect_set_tag(struct effect *e, uint32_t id, uint32_t v, const void *data);
/* Write them into e->entry (tags 0x80f70001 speed, 0x80f70002 hold; other tags are kept). 0 on success. */
int effect_set_controls(struct effect *e, int speed, int hold);
/* A project file of hdr (a blank 4.2 one when NULL) and the croutons' entries, in a new buffer. 0 on success. */
int project_build(const uint8_t *hdr, size_t hdrlen, const struct effect *fx, int nfx, uint8_t **out, size_t *n);
#endif
