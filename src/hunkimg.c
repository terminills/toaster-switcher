/* Load and relocate an AmigaOS hunk executable into a flat 68k-address image. */
#include <stdlib.h>
#include <string.h>
#include "hunkimg.h"

#define HUNK_CODE 0x3e9
#define HUNK_DATA 0x3ea
#define HUNK_BSS 0x3eb
#define HUNK_RELOC32 0x3ec
#define HUNK_SYMBOL 0x3f0
#define HUNK_DEBUG 0x3f1
#define HUNK_END 0x3f2
#define HUNK_HEADER 0x3f3

static uint32_t rd(const uint8_t *b, size_t n, size_t *o, int *err)
{
    uint32_t v;
    if (*o + 4 > n) { *err = 1; return 0; }
    v = (uint32_t)b[*o] << 24 | (uint32_t)b[*o + 1] << 16 | (uint32_t)b[*o + 2] << 8 | b[*o + 3];
    *o += 4;
    return v;
}

static void wr(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

int hunkimg_load(struct hunkimg *img, const uint8_t *b, size_t n)
{
    size_t o = 0;
    int err = 0, cur = -1, i;
    uint32_t first, last, total = 0;

    memset(img, 0, sizeof(*img));
    if (rd(b, n, &o, &err) != HUNK_HEADER) return -1;
    while (rd(b, n, &o, &err) && !err)
        ;
    rd(b, n, &o, &err);
    first = rd(b, n, &o, &err);
    last = rd(b, n, &o, &err);
    if (err || last < first || last - first + 1 > HI_MAXHUNKS) return -2;
    img->nhunks = (int)(last - first + 1);
    for (i = 0; i < img->nhunks; i++) {
        img->len[i] = (rd(b, n, &o, &err) & 0x3fffffff) * 4;
        img->start[i] = HI_BASE + total;
        total += (img->len[i] + 7) & ~7u;
    }
    if (err) return -2;
    img->size = total;
    img->mem = calloc(1, total ? total : 1);
    if (!img->mem) return -3;

    while (o < n) {
        uint32_t t = rd(b, n, &o, &err) & 0x3fffffff, len, k, th;
        if (err) break;
        switch (t) {
        case HUNK_CODE:
        case HUNK_DATA:
            len = (rd(b, n, &o, &err) & 0x3fffffff) * 4;
            cur++;
            if (err || cur >= img->nhunks || o + len > n || len > img->len[cur]) return -4;
            memcpy(img->mem + (img->start[cur] - HI_BASE), b + o, len);
            o += len;
            break;
        case HUNK_BSS:
            rd(b, n, &o, &err);
            cur++;
            if (cur >= img->nhunks) return -4;
            break;
        case HUNK_RELOC32:
            while ((k = rd(b, n, &o, &err)) && !err) {
                th = rd(b, n, &o, &err);
                if (cur < 0 || th >= (uint32_t)img->nhunks) return -5;
                while (k--) {
                    uint32_t off = rd(b, n, &o, &err);
                    uint8_t *p;
                    if (err || off + 4 > img->len[cur]) return -5;
                    p = img->mem + (img->start[cur] - HI_BASE) + off;
                    wr(p, ((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]) + img->start[th]);
                }
            }
            break;
        case HUNK_SYMBOL:
            while ((k = rd(b, n, &o, &err)) && !err)
                o += 4 * (size_t)k + 4;
            break;
        case HUNK_DEBUG:
            k = rd(b, n, &o, &err);
            o += 4 * (size_t)k;
            break;
        case HUNK_END:
            break;
        default:
            return -6;
        }
        if (err) return -7;
    }
    return cur + 1 == img->nhunks ? 0 : -8;
}

void hunkimg_free(struct hunkimg *img)
{
    free(img->mem);
    memset(img, 0, sizeof(*img));
}
