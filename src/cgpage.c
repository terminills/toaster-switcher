/* CG pages: the picture of a page or framestore file, and a scroll or crawl moved through the frame. */
#include <stdlib.h>
#include <string.h>
#include "cgpage.h"

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

/* One row of n bytes, plain or ByteRun1; the new source position, NULL when the body ends first. */
static const uint8_t *unpack_row(const uint8_t *s, const uint8_t *end, uint8_t *d, int n, int comp)
{
    if (!comp) {
        if (end - s < n) return NULL;
        memcpy(d, s, n);
        return s + n;
    }
    while (n > 0) {
        int c;
        if (s >= end) return NULL;
        c = (int8_t)*s++;
        if (c >= 0) {
            if (c + 1 > n || end - s < c + 1) return NULL;
            memcpy(d, s, c + 1);
            s += c + 1; d += c + 1; n -= c + 1;
        } else if (c != -128) {
            if (1 - c > n || s >= end) return NULL;
            memset(d, *s++, 1 - c);
            d += 1 - c; n -= 1 - c;
        }
    }
    return s;
}

static uint8_t clamp8(double v) { return v <= 0 ? 0 : v >= 1 ? 255 : (uint8_t)(v * 255 + 0.5); }

/* A framestore picture: PLTP names, for each plane, the byte and bit of the framestore it holds. Bytes 6 and 7
 * are NTSC composite samples, 4 to a subcarrier cycle, of two frames whose chroma is inverted against each other:
 * their sum is luma, their difference chroma. Byte 11, on a key page, is a 2-bit key, and it is six samples behind
 * its picture (a box ends six columns later in the key than in the picture; text has an edge of key around it).
 * BMHD's y places a key in the 480 line frame, and 752 of the 768 columns are picture. */
#define KEY_BEHIND 6
static int framestore(struct cgpage *pg, const uint8_t *body, uint32_t bodylen, int w, int h, int planes, int comp,
                      const uint8_t *pltp, uint32_t pltplen, int top)
{
    const uint8_t *p = body, *end = body + bodylen;
    int rb = (w + 15) / 16 * 2, y, pl, x, keyed = 0;
    uint8_t *row = malloc(rb), *a = calloc(w, 3), *b = a ? a + w : NULL, *k = a ? a + 2 * w : NULL;

    pg->rgba = calloc((size_t)CGPAGE_W * CGPAGE_H, 4);
    if (!row || !a || !pg->rgba || (uint32_t)planes * 2 > pltplen) goto fail;
    pg->w = CGPAGE_W;
    pg->h = CGPAGE_H;
    for (pl = 0; pl < planes; pl++)
        if (pltp[2 * pl] == 11) keyed = 1;
    for (x = 0; x < CGPAGE_W * CGPAGE_H; x++)       /* a frame is opaque, a key only where its key says */
        pg->rgba[x * 4 + 3] = keyed ? 0 : 255;
    for (y = 0; y < h; y++) {
        int fy = top + y, sign = ((fy + 1) >> 1 & 1) ? 1 : -1;
        memset(a, 0, (size_t)w * 3);
        for (pl = 0; pl < planes; pl++) {
            int byte = pltp[2 * pl], bit = pltp[2 * pl + 1] & 7;
            uint8_t *dst = byte == 6 ? a : byte == 7 ? b : byte == 11 ? k : NULL;
            if (!(p = unpack_row(p, end, row, rb, comp))) goto fail;
            if (dst)
                for (x = 0; x < w; x++)
                    if (row[x >> 3] >> (7 - (x & 7)) & 1) dst[x] |= (uint8_t)(1 << bit);
        }
        if (fy < 0 || fy >= CGPAGE_H) continue;
        for (x = 0; x < CGPAGE_W && x < w; x++) {
            uint8_t *d = pg->rgba + ((size_t)fy * CGPAGE_W + x) * 4;
            double c[4] = { 0, 0, 0, 0 }, Y, I, Q;
            int j;
            for (j = x - 1; j <= x + 2; j++) {      /* chroma of the four samples around x, by their phase */
                int jx = j < 0 ? 0 : j >= w ? w - 1 : j;
                c[j & 3] = (a[jx] - b[jx]) / 2.0;
            }
            Y = ((a[x] + b[x]) / 2.0 - 70.5) / 129.5;
            I = sign * (c[0] - c[2]) / 2.0 / 129.5;
            Q = sign * (c[1] - c[3]) / 2.0 / 129.5;
            d[0] = clamp8(Y + 0.956 * I + 0.621 * Q);
            d[1] = clamp8(Y - 0.272 * I - 0.647 * Q);
            d[2] = clamp8(Y - 1.106 * I + 1.703 * Q);
            if (keyed) d[3] = (uint8_t)((k[x + KEY_BEHIND < w ? x + KEY_BEHIND : w - 1] & 3) * 85);
        }
    }
    free(row);
    free(a);
    return 0;
fail:
    free(row);
    free(a);
    return -1;
}

int cgpage_decode(struct cgpage *pg, const uint8_t *d, size_t n)
{
    size_t o, end, bmhd = 0, body = 0, pltp = 0;
    uint32_t bodyl = 0, pltpl = 0;
    int w, h, np, mask, comp, rb, x, y, pl;
    const uint8_t *p, *bend;
    uint8_t *row;

    memset(pg, 0, sizeof(*pg));
    if (n < 12 || memcmp(d, "FORM", 4) || memcmp(d + 8, "ILBM", 4)) return -1;
    end = 8 + (size_t)be32(d + 4);
    if (end > n) end = n;
    for (o = 12; o + 8 <= end; ) {
        uint32_t l = be32(d + o + 4);
        if (l > end - o - 8) break;
        if (!memcmp(d + o, "BMHD", 4) && l >= 20) bmhd = o + 8;
        else if (!memcmp(d + o, "PLTP", 4)) { pltp = o + 8; pltpl = l; }
        else if (!memcmp(d + o, "BODY", 4)) { body = o + 8; bodyl = l; }
        o += 8 + (size_t)l + (l & 1);
    }
    if (!bmhd || !body) return -1;
    w = be16(d + bmhd); h = be16(d + bmhd + 2); np = d[bmhd + 8]; mask = d[bmhd + 9]; comp = d[bmhd + 10];
    if (!w || !h || comp > 1) return -1;
    if (pltp && np <= 24) {
        if (framestore(pg, d + body, bodyl, w, h, np, comp, d + pltp, pltpl, (int16_t)be16(d + bmhd + 6))) goto fail;
        return 0;
    }
    if (np != 24 && np != 32) return -1;
    rb = (w + 15) / 16 * 2;
    pg->w = w;
    pg->h = h;
    pg->top = (int16_t)be16(d + bmhd + 6);
    pg->rgba = calloc((size_t)w * h, 4);
    if (!pg->rgba || !(row = malloc(rb))) goto fail;
    p = d + body;
    bend = p + bodyl;
    for (y = 0; y < h; y++) {
        uint8_t *px = pg->rgba + (size_t)y * w * 4;
        for (pl = 0; pl < np + (mask == 1); pl++) {
            if (!(p = unpack_row(p, bend, row, rb, comp))) {
                free(row);
                goto fail;
            }
            for (x = 0; x < w; x++) {
                int bit = row[x >> 3] >> (7 - (x & 7)) & 1;
                if (pl < np) px[x * 4 + pl / 8] |= (uint8_t)(bit << (pl & 7));
                else if (np == 24) px[x * 4 + 3] = bit ? 255 : 0;
            }
        }
        if (mask != 1 && np == 24)
            for (x = 0; x < w; x++) px[x * 4 + 3] = 255;
    }
    free(row);
    return 0;
fail:
    cgpage_free(pg);
    return -1;
}

void cgpage_free(struct cgpage *p)
{
    free(p->rgba);
    free(p->scaled);
    p->rgba = p->scaled = NULL;
}

int cgpage_speed(int kind, int speed)
{
    if (speed < 0) speed = 0;
    if (kind == CGP_CRAWL) return 4 << (speed > 3 ? 3 : speed);
    return speed ? 2 * (speed > 8 ? 8 : speed) : 1;
}

int cgpage_at(const struct cgpage *p, long fields, long *pos2)
{
    long frame = p->kind == CGP_SCROLL ? CGPAGE_H : CGPAGE_W, len = p->kind == CGP_SCROLL ? p->h : p->w;
    long total = (frame + len) * 2, at;

    *pos2 = 0;
    if (p->kind == CGP_STILL || p->speed2 <= 0) return 1;
    if (fields < 0) fields = 0;
    if (p->mode == CGP_LOOP) fields %= (total + p->speed2 - 1) / p->speed2;
    else if (fields > total) fields = total;        /* past its end at any speed */
    at = fields * p->speed2;
    if (p->mode == CGP_FREEZE && p->kind == CGP_SCROLL) {
        if (at > total - 2 * CGPAGE_SAFE_BOTTOM) at = total - 2 * CGPAGE_SAFE_BOTTOM;
    } else if (at >= total) {
        return 0;
    }
    *pos2 = at - frame * 2;
    return 1;
}

static long floordiv(long a, long b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* The picture for an ow x oh output, each pixel the mean of the part of the picture under it. */
static uint8_t *scale(const struct cgpage *p, int ow, int oh, int *sw, int *sh)
{
    int w2 = (int)(((int64_t)p->w * ow + CGPAGE_W - 1) / CGPAGE_W), h2 = (int)(((int64_t)p->h * oh + CGPAGE_H - 1) / CGPAGE_H);
    uint8_t *out = malloc((size_t)w2 * h2 * 4), *d = out;
    int x, y;

    if (!out) return NULL;
    for (y = 0; y < h2; y++) {
        int64_t y0 = (int64_t)y * CGPAGE_H * 256 / oh, y1 = (int64_t)(y + 1) * CGPAGE_H * 256 / oh;
        for (x = 0; x < w2; x++, d += 4) {
            int64_t x0 = (int64_t)x * CGPAGE_W * 256 / ow, x1 = (int64_t)(x + 1) * CGPAGE_W * 256 / ow, sx, sy;
            uint64_t r = 0, g = 0, b = 0, a = 0, all = (uint64_t)(x1 - x0) * (uint64_t)(y1 - y0);
            for (sy = y0 >> 8; sy <= (y1 - 1) >> 8 && sy < p->h; sy++) {
                int64_t ya = sy << 8 > y0 ? sy << 8 : y0, yb = (sy + 1) << 8 < y1 ? (sy + 1) << 8 : y1;
                const uint8_t *s = p->rgba + ((size_t)sy * p->w + (size_t)(x0 >> 8)) * 4;
                for (sx = x0 >> 8; sx <= (x1 - 1) >> 8 && sx < p->w; sx++, s += 4) {
                    int64_t xa = sx << 8 > x0 ? sx << 8 : x0, xb = (sx + 1) << 8 < x1 ? (sx + 1) << 8 : x1;
                    uint64_t wa = (uint64_t)((xb - xa) * (yb - ya)) * s[3];
                    r += wa * s[0]; g += wa * s[1]; b += wa * s[2]; a += wa;
                }
            }
            d[0] = (uint8_t)(r / (all * 255));
            d[1] = (uint8_t)(g / (all * 255));
            d[2] = (uint8_t)(b / (all * 255));
            d[3] = (uint8_t)(a / all);
        }
    }
    *sw = w2;
    *sh = h2;
    return out;
}

void cgpage_blend(struct cgpage *p, long pos2, uint32_t *buf, int ow, int oh, int level, int max)
{
    int x, y, dx = 0, dy = 0, x0, x1;

    if (level <= 0 || !p->rgba || ow <= 0 || oh <= 0) return;
    if (!p->scaled || p->ow != ow || p->oh != oh) {
        free(p->scaled);
        p->scaled = scale(p, ow, oh, &p->sw, &p->sh);
        p->ow = ow;
        p->oh = oh;
    }
    if (!p->scaled) return;
    if (p->kind == CGP_SCROLL) {
        dy = (int)floordiv(pos2 * oh, 2 * CGPAGE_H);
    } else if (p->kind == CGP_CRAWL) {
        dx = (int)floordiv(pos2 * ow, 2 * CGPAGE_W);
        dy = -(p->top * oh / CGPAGE_H);
    }
    x0 = dx < 0 ? -dx : 0;
    x1 = p->sw - dx < ow ? p->sw - dx : ow;
    if (x0 >= x1) return;
    for (y = 0; y < oh; y++) {
        const uint8_t *s;
        uint32_t *d = buf + (size_t)y * ow + x0;
        if (y + dy < 0 || y + dy >= p->sh) continue;
        s = p->scaled + ((size_t)(y + dy) * p->sw + x0 + dx) * 4;
        for (x = x0; x < x1; x++, s += 4, d++) {
            int a = s[3] * level / max;
            if (!a) continue;
            *d = (((*d >> 16 & 255) * (255 - a) / 255 + s[0] * level / max) << 16) |
                 (((*d >> 8 & 255) * (255 - a) / 255 + s[1] * level / max) << 8) |
                 ((*d & 255) * (255 - a) / 255 + s[2] * level / max);
        }
    }
}

uint32_t *cgpage_frame(struct cgpage *p, int ow, int oh)
{
    uint32_t *rgb = calloc((size_t)ow * oh, 4);
    if (rgb) cgpage_blend(p, 0, rgb, ow, oh, 1, 1);
    return rgb;
}

uint32_t *cgpage_rgb(const struct cgpage *p)
{
    uint32_t *rgb = p->rgba ? malloc((size_t)p->w * p->h * 4) : NULL;
    size_t i, n = (size_t)p->w * p->h;

    for (i = 0; rgb && i < n; i++) {
        const uint8_t *s = p->rgba + i * 4;
        rgb[i] = (uint32_t)(s[0] * s[3] / 255) << 16 | (uint32_t)(s[1] * s[3] / 255) << 8 | (uint32_t)(s[2] * s[3] / 255);
    }
    return rgb;
}
