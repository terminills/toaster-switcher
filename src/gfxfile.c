/* ILBM pictures and Amiga colour fonts from the user's Toaster install. */
#include <stdlib.h>
#include <string.h>
#include "gfxfile.h"

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

int ilbm_decode(const uint8_t *d, size_t n, struct picture *pic)
{
    size_t o, bmhd = 0, cmap = 0, body = 0;
    uint32_t bodyl = 0, cmapl = 0;
    int w, h, np, rb, x, y, p, comp, rows;
    uint8_t *raw;
    size_t need, i, k;

    memset(pic, 0, sizeof(*pic));
    if (n < 12 || memcmp(d, "FORM", 4) || memcmp(d + 8, "ILBM", 4)) return -1;
    for (o = 12; o + 8 <= n; ) {
        uint32_t l = be32(d + o + 4);
        if (o + 8 + l > n) break;
        if (!memcmp(d + o, "BMHD", 4) && l >= 20) bmhd = o + 8;
        else if (!memcmp(d + o, "CMAP", 4)) { cmap = o + 8; cmapl = l; }
        else if (!memcmp(d + o, "BODY", 4)) { body = o + 8; bodyl = l; }
        o += 8 + l + (l & 1);
    }
    if (!bmhd || !body) return -1;
    w = be16(d + bmhd); h = be16(d + bmhd + 2); np = d[bmhd + 8]; comp = d[bmhd + 10];
    if (w <= 0 || h <= 0 || (!(np >= 1 && np <= 8 && cmap) && np != 24)) return -1;
    rb = ((w + 15) / 16) * 2;
    rows = np + (d[bmhd + 9] == 1);         /* masking 1: a mask row follows each line's planes */
    need = (size_t)rb * rows * h;
    if (!(raw = malloc(need))) return -1;
    for (i = 0, k = 0; k < need && i < bodyl; ) {
        int c = d[body + i++];
        size_t r;
        if (!comp) { size_t m = bodyl < need ? bodyl : need; memcpy(raw, d + body, m); k = m; break; }
        if (c < 128) {                      /* a last run past the picture is clipped */
            if (i + c + 1 > bodyl) break;
            r = k + c + 1 > need ? need - k : (size_t)c + 1;
            memcpy(raw + k, d + body + i, r); i += c + 1; k += r;
        } else if (c > 128) {
            if (i >= bodyl) break;
            r = k + 257 - c > need ? need - k : (size_t)(257 - c);
            memset(raw + k, d[body + i++], r); k += r;
        }
    }
    if (k < need) { free(raw); return -1; }
    if (!(pic->rgb = malloc((size_t)w * h * 4))) { free(raw); return -1; }
    pic->w = w; pic->h = h;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint32_t v = 0;
            for (p = 0; p < np; p++)
                v |= (uint32_t)((raw[(size_t)(y * rows + p) * rb + x / 8] >> (7 - x % 8)) & 1) << p;
            if (np == 24)                   /* deep ILBM: planes 0-7 red, 8-15 green, 16-23 blue */
                pic->rgb[(size_t)y * w + x] = (v & 255) << 16 | (v >> 8 & 255) << 8 | v >> 16;
            else
                pic->rgb[(size_t)y * w + x] = v * 3 + 2 < cmapl ?
                    (uint32_t)d[cmap + v * 3] << 16 | d[cmap + v * 3 + 1] << 8 | d[cmap + v * 3 + 2] : 0;
        }
    free(raw);
    return 0;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

int ilbm_encode24(const uint32_t *rgb, int w, int h, uint8_t **out, size_t *n)
{
    int rb = ((w + 15) / 16) * 2, x, y, p;
    size_t body = (size_t)rb * 24 * h, total = 12 + 8 + 20 + 8 + body;
    uint8_t *d, *b;
    if (w <= 0 || h <= 0 || !(d = calloc(1, total))) return -1;
    memcpy(d, "FORM", 4); put32(d + 4, total - 8); memcpy(d + 8, "ILBM", 4);
    memcpy(d + 12, "BMHD", 4); put32(d + 16, 20);
    d[20] = w >> 8; d[21] = w; d[22] = h >> 8; d[23] = h;
    d[28] = 24;                             /* planes; masking, compression, pad, transparent colour 0 */
    d[34] = 10; d[35] = 11;                 /* aspect */
    d[36] = w >> 8; d[37] = w; d[38] = h >> 8; d[39] = h;
    memcpy(d + 40, "BODY", 4); put32(d + 44, body);
    b = d + 48;
    for (y = 0; y < h; y++)
        for (p = 0; p < 24; p++) {
            uint8_t *row = b + ((size_t)y * 24 + p) * rb;
            int sh = p < 8 ? 16 + p : p < 16 ? 8 + p - 8 : p - 16;
            for (x = 0; x < w; x++)
                if ((rgb[(size_t)y * w + x] >> sh) & 1) row[x / 8] |= 0x80 >> (x % 8);
        }
    *out = d;
    *n = total;
    return 0;
}

void picture_free(struct picture *pic)
{
    free(pic->rgb);
    memset(pic, 0, sizeof(*pic));
}

/* DiskFontHeader after the font file's moveq #-1,d0 / rts: Node 14, FileID 2, Revision 2, Segment 4, Name 32,
 * then the TextFont. TextFont: Message 20, YSize 20, Style 22, Flags 23, XSize 24, Baseline 26, BoldSmear 28,
 * Accessors 30, LoChar 32, HiChar 33, CharData 34, Modulo 38, CharLoc 40, CharSpace 44, CharKern 48;
 * ColorTextFont: Flags 52, Depth 54, FgColor 55, Low 56, High 57, PlanePick 58, PlaneOnOff 59, Colors 60,
 * CharData[8] 64. */
#define DFH_TF 54
#define FPF_COLORFONT 0x40

int colorfont_load(struct colorfont *f, const uint8_t *buf, size_t n)
{
    const struct hunkimg *h = &f->img;
    uint32_t tf;
    int i;
    memset(f, 0, sizeof(*f));
    if (hunkimg_load(&f->img, buf, n) < 0) return -1;
    tf = f->tf = hi_hunk(h, 0) + 4 + DFH_TF;
    if (!hi_valid(h, tf, 96)) { colorfont_free(f); return -1; }
    f->ysize = hi_w(h, tf + 20); f->xsize = hi_w(h, tf + 24); f->baseline = hi_w(h, tf + 26);
    f->lo = hi_b(h, tf + 32); f->hi = hi_b(h, tf + 33);
    f->modulo = hi_w(h, tf + 38);
    f->charloc = hi_l(h, tf + 40); f->charspace = hi_l(h, tf + 44); f->charkern = hi_l(h, tf + 48);
    if (hi_b(h, tf + 23) & FPF_COLORFONT) {
        f->depth = hi_b(h, tf + 54);
        f->planepick = hi_b(h, tf + 58);
        f->planeonoff = hi_b(h, tf + 59);
        for (i = 0; i < 8 && i < f->depth; i++) f->chardata[i] = hi_l(h, tf + 64 + 4 * i);
    } else {
        f->depth = 1; f->planepick = 1; f->planeonoff = 0;
        f->chardata[0] = hi_l(h, tf + 34);
    }
    if (f->ysize <= 0 || f->hi < f->lo || !f->charloc || f->depth < 1 || f->depth > 8) { colorfont_free(f); return -1; }
    return 0;
}

void colorfont_free(struct colorfont *f)
{
    hunkimg_free(&f->img);
}

static int glyph(const struct colorfont *f, int c)
{
    return c < f->lo || c > f->hi ? f->hi - f->lo + 1 : c - f->lo;     /* the font's "not available" glyph */
}

int colorfont_width(const struct colorfont *f, const char *s)
{
    int x = 0;
    for (; *s; s++) {
        int g = glyph(f, (unsigned char)*s);
        x += (f->charkern ? (int16_t)hi_w(&f->img, f->charkern + 2 * g) : 0) +
             (f->charspace ? (int16_t)hi_w(&f->img, f->charspace + 2 * g) : f->xsize);
    }
    return x;
}

int colorfont_draw(const struct colorfont *f, uint32_t *rgb, int stride, int maxw, int maxh, int x, int y, const char *s,
                   const uint32_t *pal)
{
    const struct hunkimg *h = &f->img;
    for (; *s; s++) {
        int g = glyph(f, (unsigned char)*s), off = hi_w(h, f->charloc + 4 * g), wid = hi_w(h, f->charloc + 4 * g + 2);
        int yy, b, p;
        x += f->charkern ? (int16_t)hi_w(h, f->charkern + 2 * g) : 0;
        for (yy = 0; yy < f->ysize; yy++)
            for (b = 0; b < wid; b++) {
                int bit = off + b, v = 0, pi = 0;
                for (p = 0; p < 8; p++) {
                    if (f->planepick & (1 << p)) {
                        if (pi < f->depth &&
                            (hi_b(h, f->chardata[pi] + (uint32_t)yy * f->modulo + bit / 8) >> (7 - bit % 8)) & 1)
                            v |= 1 << p;
                        pi++;
                    } else if (f->planeonoff & (1 << p)) {
                        v |= 1 << p;
                    }
                }
                if (pal[v & 7] != CF_TRANSPARENT && x + b >= 0 && x + b < maxw && y + yy >= 0 && y + yy < maxh)
                    rgb[(size_t)(y + yy) * stride + x + b] = pal[v & 7];
            }
        x += f->charspace ? (int16_t)hi_w(h, f->charspace + 2 * g) : f->xsize;
    }
    return x;
}
