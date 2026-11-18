/* NewTek interface resources read at runtime: ILBM pictures (Edit_Support/Gadget64.pic) and Amiga colour fonts
 * (InterfaceLG.font and friends). */
#ifndef GFXFILE_H
#define GFXFILE_H
#include <stdint.h>
#include <stddef.h>
#include "hunkimg.h"

struct picture {
    int w, h;
    uint32_t *rgb;              /* 0x00RRGGBB */
};
/* Decode an IFF ILBM (ByteRun1 or plain; CMAP colours, or 24 planes of red, green, blue). 0 on success. */
int ilbm_decode(const uint8_t *buf, size_t n, struct picture *pic);
/* A w x h 0x00RRGGBB picture as an uncompressed 24-plane ILBM in a new buffer (*out, *n; free() it). 0 on success. */
int ilbm_encode24(const uint32_t *rgb, int w, int h, uint8_t **out, size_t *n);
void picture_free(struct picture *pic);

struct colorfont {
    struct hunkimg img;         /* the font file, relocated */
    uint32_t tf;                /* struct TextFont (ColorTextFont) */
    int ysize, xsize, baseline, lo, hi, modulo, depth, planepick, planeonoff;
    uint32_t chardata[8], charloc, charspace, charkern;
};
/* Load a font file (a hunk file such as Fonts/InterfaceLG/11). 0 on success. */
int colorfont_load(struct colorfont *f, const uint8_t *buf, size_t n);
void colorfont_free(struct colorfont *f);
/* Width in pixels of s. */
int colorfont_width(const struct colorfont *f, const char *s);
/* Draw s at (x, y top) into rgb (stride pixels per row); pixel values index pal (8 entries); an entry of
 * CF_TRANSPARENT leaves the pixel alone. NewTek's interface fonts draw their letters in pen 0 on pen 2.
 * Returns the x after the text. */
#define CF_TRANSPARENT 0xff000000u
int colorfont_draw(const struct colorfont *f, uint32_t *rgb, int stride, int maxw, int maxh, int x, int y, const char *s,
                   const uint32_t *pal);
#endif
