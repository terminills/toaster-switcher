/* A relocated image of a 68k hunk executable's hunks: the original data keeps its big-endian
 * layout and 32-bit 68k addresses; converted code reads it through these accessors. */
#ifndef HUNKIMG_H
#define HUNKIMG_H
#include <stdint.h>
#include <stddef.h>

#define HI_BASE 0x00100000u     /* 68k address of the first hunk */
#define HI_MAXHUNKS 64

struct hunkimg {
    uint8_t *mem;               /* the whole image; 68k address a lives at mem[a - HI_BASE] */
    uint32_t size;
    int nhunks;
    uint32_t start[HI_MAXHUNKS];   /* 68k address of each hunk */
    uint32_t len[HI_MAXHUNKS];     /* allocated size (incl. BSS) */
};

/* Parse buf (a hunk executable read from disk) into img. Returns 0 or a negative error. */
int hunkimg_load(struct hunkimg *img, const uint8_t *buf, size_t n);
void hunkimg_free(struct hunkimg *img);

static inline int hi_valid(const struct hunkimg *h, uint32_t a, uint32_t n)
{
    return a >= HI_BASE && a - HI_BASE + n <= h->size;
}
static inline uint8_t hi_b(const struct hunkimg *h, uint32_t a)
{
    return hi_valid(h, a, 1) ? h->mem[a - HI_BASE] : 0;
}
static inline uint16_t hi_w(const struct hunkimg *h, uint32_t a)
{
    const uint8_t *p;
    if (!hi_valid(h, a, 2)) return 0;
    p = h->mem + (a - HI_BASE);
    return (uint16_t)(p[0] << 8 | p[1]);
}
static inline uint32_t hi_l(const struct hunkimg *h, uint32_t a)
{
    const uint8_t *p;
    if (!hi_valid(h, a, 4)) return 0;
    p = h->mem + (a - HI_BASE);
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static inline uint32_t hi_hunk(const struct hunkimg *h, int n)
{
    return n >= 0 && n < h->nhunks ? h->start[n] : 0;
}
#endif
