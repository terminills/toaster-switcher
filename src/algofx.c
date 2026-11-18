/* ALGO croutons drawn natively from their AlgoFX parameter blocks (see algofx.h). */
#include <math.h>
#include <string.h>
#include "algofx.h"

#define TWO_PI 6.283185307179586
#define BLK_MOSAICX 11
#define BLK_MOSAICY 12
#define BLK_FADE 23
#define FL_REVERSE 4
#define FL_FADE 16
#define FL_DIP 8
#define FL_CROSS 128
#define FL_TILE 0x600

int algofx_parse(struct algofx *a, int type, int mode, const uint8_t *blob, size_t n)
{
    size_t nw = n / 2, i, k;
    memset(a, 0, sizeof(*a));
    a->type = type;
    a->mode = mode;
    if (nw < 4 || nw > ALGO_MAXWORDS) return -1;
    for (k = 0; k < nw; k++) a->w[k] = (int16_t)(blob[2 * k] << 8 | blob[2 * k + 1]);
    i = (uint16_t)a->w[1] / 2;
    if (i < 4 || i >= nw) return -1;
    a->flags = (uint16_t)a->w[3];
    a->nobj = i >= 5 ? a->w[4] : 1;
    while (i < nw && a->w[i]) {
        struct algo_block *b;
        if (a->nblk == ALGO_MAXBLK) return -1;
        b = &a->blk[a->nblk++];
        b->id = a->w[i++];
        while (i < nw && a->w[i]) {
            struct algo_track *t;
            int len;
            if (b->n == ALGO_MAXPAR) return -1;
            t = &b->p[b->n++];
            t->kind = a->w[i];
            t->off = i + 1;
            switch (t->kind) {
            case 2: len = 1; break;
            case 3: len = 8; break;
            case 4: case 6: len = 10; break;
            case 5: len = 20; break;
            case 7: case 8: len = i + 1 < nw ? (uint16_t)a->w[i + 1] / 2 : -1; break;
            default: len = -1;
            }
            if (len < 1 || i + 1 + len > nw) return -1;
            i += 1 + len;
        }
        i++;
    }
    return a->nblk ? 0 : -1;
}

static double fix(const struct algofx *a, int o)
{
    return (int32_t)((uint32_t)(uint16_t)a->w[o] << 16 | (uint16_t)a->w[o + 1]) / 65536.0;
}

static double frac(const struct algofx *a, int o)
{
    return (uint16_t)a->w[o] / 65536.0;
}

static double ramp(const struct algofx *a, int o, double s)
{
    double v = fix(a, o) + fix(a, o + 2) * s, max = fix(a, o + 6);
    return v > max ? max : v;
}

static double eval(const struct algofx *a, const struct algo_track *t, double s)
{
    int o = t->off, e;
    switch (t->kind) {
    case 2:
        return a->w[o];
    case 3:
        return ramp(a, o, s);
    case 4: {
        double base = fix(a, o + 8), v0 = base + fix(a, o), v1 = v0 + fix(a, o + 2);
        if (v0 > 0 && v1 > 0) return 1 / ((1 - s) / v0 + s / v1);
        return v0 + (v1 - v0) * s;
    }
    case 5:
        return fix(a, o + 18) + ramp(a, o + 10, s) * sin(TWO_PI * (fix(a, o + 8) + fix(a, o) * s));
    case 6: {
        double from = frac(a, o + 8), to = a->w[o + 9] ? frac(a, o + 9) : 1.0;
        s = to <= from ? (s >= from) : s <= from ? 0 : s >= to ? 1 : (s - from) / (to - from);
        return ramp(a, o, s);
    }
    case 7:                         /* keyframes, linear between them */
        e = o + (uint16_t)a->w[o] / 2;
        for (o++; o + 3 < e && (uint16_t)a->w[o + 2] != 0xffff; o += 2) {
            double t0 = frac(a, o), t1 = frac(a, o + 2);
            if (s < t1) return s <= t0 ? a->w[o + 1] : a->w[o + 1] + (a->w[o + 3] - a->w[o + 1]) * (s - t0) / (t1 - t0);
        }
        return a->w[o + 1];
    case 8:                         /* a ramp per segment */
        e = o + (uint16_t)a->w[o] / 2;
        for (o++; o + 9 < e && (uint16_t)a->w[o + 5] != 0xffff && s >= frac(a, o + 5); o += 5)
            ;
        {
            double t0 = frac(a, o), t1 = o + 9 < e && (uint16_t)a->w[o + 5] != 0xffff ? frac(a, o + 5) : 1.0;
            double f = t1 > t0 ? (s - t0) / (t1 - t0) : 1;
            if (f < 0) f = 0;
            if (f > 1) f = 1;
            return fix(a, o + 1) + fix(a, o + 3) * f;
        }
    }
    return 0;
}

static double par(const struct algofx *a, const struct algo_block *b, int i, double s, double dflt)
{
    return b && i < b->n ? eval(a, &b->p[i], s) : dflt;
}

static const struct algo_block *block(const struct algofx *a, int id)
{
    int i;
    for (i = 0; i < a->nblk; i++)
        if (a->blk[i].id == id) return &a->blk[i];
    return NULL;
}

/* Object k's X and Y blocks: ids count up from xbase and ybase; one Y block alone serves every object. */
static void obj_blocks(const struct algofx *a, int k, int xbase, int ybase, const struct algo_block **x,
                       const struct algo_block **y)
{
    *x = block(a, xbase + k);
    *y = block(a, ybase + k);
    if (!*y) *y = block(a, ybase);
}

static uint32_t mix(uint32_t p, uint32_t q, double f)
{
    uint32_t o = 0;
    int c;
    if (f <= 0) return p;
    if (f >= 1) return q;
    for (c = 0; c < 24; c += 8) {
        int vp = (p >> c) & 255, vq = (q >> c) & 255;
        o |= (uint32_t)(vp + (vq - vp) * f + 0.5) << c;
    }
    return o;
}

static void range(double place, double anchor, double size, double *lo, double *hi)
{
    if (size < 0) size = -size;
    if (place == 32767) { *lo = anchor - size / 2; *hi = anchor + size / 2; }
    else if (place < 0) { *lo = anchor - size + 1; *hi = anchor + 1; }
    else { *lo = anchor; *hi = anchor + size; }
}

static void render_wipe(const struct algofx *a, double s, const uint32_t *pa, const uint32_t *pb, uint32_t *out,
                        int w, int h)
{
    double lox[16], hix[16], loy[16], hiy[16];
    int x, y, k, inb = 1, nobj = a->nobj > 0 && a->nobj <= 16 ? a->nobj : 1, cross = a->flags & FL_CROSS;
    for (k = 0; k < nobj; k++) {
        const struct algo_block *bx, *by;
        obj_blocks(a, k, 13, 22, &bx, &by);
        if (!bx || !by) { lox[k] = hix[k] = loy[k] = hiy[k] = -1; continue; }
        range(par(a, bx, 1, s, 0), par(a, bx, 0, s, 0) + par(a, bx, 4, s, 0), par(a, bx, 5, s, 736), &lox[k], &hix[k]);
        range(par(a, by, 1, s, 0), par(a, by, 0, s, 0) + par(a, by, 4, s, 0), par(a, by, 5, s, 481), &loy[k], &hiy[k]);
        if (!k) inb = par(a, bx, 6, s, 1) != 0;
    }
    for (y = 0; y < h; y++) {
        double uy = (y + 0.5) * 481 / h;
        for (x = 0; x < w; x++) {
            double ux = (x + 0.5) * 736 / w;
            int in = 0;
            for (k = 0; k < nobj && !in; k++) {
                int ix = ux >= lox[k] && ux < hix[k], iy = uy >= loy[k] && uy < hiy[k];
                in = cross ? ix || iy : ix && iy;
            }
            out[y * w + x] = (in == inb) ? pb[y * w + x] : pa[y * w + x];
        }
    }
}

struct axis { double off, lo, hi, sc, clo, chi, dc, size; };

static void axis_at(const struct algofx *a, const struct algo_block *b, double s, struct axis *x, int y)
{
    x->off = par(a, b, 2, s, 0);
    x->lo = par(a, b, 3, s, y ? 2 : 5);
    x->hi = par(a, b, 4, s, y ? 238 : 189);
    x->sc = par(a, b, 5, s, y ? 120 : 97);
    x->clo = par(a, b, 6, s, y ? 1 : 0);
    x->chi = par(a, b, 7, s, y ? 240 : 183);
    x->dc = par(a, b, 8, s, y ? 120 : 94);
    x->size = par(a, b, 9, s, y ? 237 : 185);
}

static double wrap(double v, double lo, double hi)
{
    double span = hi - lo;
    return span > 0 ? lo + fmod(fmod(v - lo, span) + span, span) : v;
}

static void render_move(const struct algofx *a, double s, const uint32_t *mov, const uint32_t *bg, uint32_t bgcol,
                        int bgmatte, uint32_t *out, int w, int h)
{
    struct axis X[16], Y[16];
    int have[16], x, y, k, nobj = a->nobj > 0 && a->nobj <= 16 ? a->nobj : 1, tile = a->flags & FL_TILE;
    const struct algo_block *fb = block(a, BLK_FADE);
    double fade = fb ? par(a, fb, 0, s, 255) / 255 : 1;
    for (k = 0; k < nobj; k++) {
        const struct algo_block *bx, *by;
        obj_blocks(a, k, 1, 10, &bx, &by);
        if ((have[k] = bx && by)) {
            axis_at(a, bx, s, &X[k], 0);
            axis_at(a, by, s, &Y[k], 1);
        }
    }
    for (y = 0; y < h; y++) {
        double uy = 120 + ((y + 0.5) / h - 0.5) * 237;
        for (x = 0; x < w; x++) {
            double ux = 94 + ((x + 0.5) / w - 0.5) * 185;
            uint32_t back = bgmatte ? bgcol : bg[y * w + x], o = back;
            for (k = 0; k < nobj; k++) {
                const struct axis *ax = &X[k], *ay = &Y[k];
                double sx, sy;
                int ix, iy;
                if (!have[k] || fabs(ax->size) < 0.25 || fabs(ay->size) < 0.25) continue;
                if (ux < ax->clo || ux > ax->chi + 1 || uy < ay->clo || uy > ay->chi + 1) continue;
                sx = ax->sc + (ux - ax->dc - ax->off) * (ax->hi - ax->lo) / ax->size;
                sy = ay->sc + (uy - ay->dc - ay->off) * (ay->hi - ay->lo) / ay->size;
                if (tile) {
                    sx = wrap(sx, ax->lo, ax->hi);
                    sy = wrap(sy, ay->lo, ay->hi);
                } else if (sx < ax->lo || sx > ax->hi || sy < ay->lo || sy > ay->hi)
                    continue;
                ix = (int)(((sx - 97) / 184 + 0.5) * w);
                iy = (int)(((sy - 120) / 236 + 0.5) * h);
                if (ix < 0) ix = 0;
                if (ix >= w) ix = w - 1;
                if (iy < 0) iy = 0;
                if (iy >= h) iy = h - 1;
                o = mix(back, mov[iy * w + ix], fade);
                break;
            }
            out[y * w + x] = o;
        }
    }
}

/* Blocks 11 and 12: {lo, hi, grid origin, cell} in dest units; cells inside take their centre pixel. */
static void mosaic(const struct algofx *a, double s, uint32_t *out, int w, int h)
{
    const struct algo_block *bx = block(a, BLK_MOSAICX), *by = block(a, BLK_MOSAICY);
    double xlo, xhi, xo, xc, ylo, yhi, yo, yc;
    int x, y;
    if (!bx || !by) return;
    xlo = par(a, bx, 0, s, 0); xhi = par(a, bx, 1, s, 184); xo = par(a, bx, 2, s, 0); xc = par(a, bx, 3, s, 1);
    ylo = par(a, by, 0, s, 0); yhi = par(a, by, 1, s, 240); yo = par(a, by, 2, s, 0); yc = par(a, by, 3, s, 1);
    if (xc < 1.5 && yc < 1.5) return;
    if (xc < 1) xc = 1;
    if (yc < 1) yc = 1;
    for (y = 0; y < h; y++) {
        double uy = 120 + ((y + 0.5) / h - 0.5) * 237, cy;
        int sy;
        if (uy < ylo || uy > yhi) continue;
        cy = yo + (floor((uy - yo) / yc) + 0.5) * yc;
        sy = (int)(((cy - 120) / 237 + 0.5) * h);
        if (sy < 0) sy = 0;
        if (sy >= h) sy = h - 1;
        for (x = 0; x < w; x++) {
            double ux = 94 + ((x + 0.5) / w - 0.5) * 185, cx;
            int sx;
            if (ux < xlo || ux > xhi) continue;
            cx = xo + (floor((ux - xo) / xc) + 0.5) * xc;
            sx = (int)(((cx - 94) / 185 + 0.5) * w);
            if (sx < 0) sx = 0;
            if (sx >= w) sx = w - 1;
            out[y * w + x] = out[sy * w + sx];
        }
    }
}

void algofx_render(const struct algofx *a, int t, int tmax, const uint32_t *pa, const uint32_t *pb, uint32_t *out,
                   int w, int h, uint32_t matte)
{
    double s = (double)t / tmax;
    int k;
    if (a->flags & FL_REVERSE && a->type != 2) s = 1 - s;

    switch (a->type) {
    case 2: {
        const struct algo_block *fb = block(a, BLK_FADE);
        double f = fb ? par(a, fb, 0, s, 0) / 255 : s;
        for (k = 0; k < w * h; k++)
            out[k] = a->flags & FL_DIP ? (f < 0.5 ? mix(pa[k], matte, f * 2) : mix(matte, pb[k], f * 2 - 1))
                                       : mix(pa[k], pb[k], f);
        return;
    }
    case 1:
        render_wipe(a, s, pa, pb, out, w, h);
        return;
    }
    switch (a->mode) {
    case 3:
        render_move(a, s, pa, pb, 0, 0, out, w, h);
        break;
    case 6:
        render_move(a, s, pa, NULL, matte, 1, out, w, h);
        break;
    case 5:                         /* the outgoing picture to the midpoint, then the incoming one back */
        if (t * 2 < tmax) render_move(a, 2.0 * t / tmax, pa, pa, 0, 0, out, w, h);
        else render_move(a, 2.0 * (tmax - t) / tmax, pb, pb, 0, 0, out, w, h);
        s = t * 2 < tmax ? 2.0 * t / tmax : 2.0 * (tmax - t) / tmax;
        break;
    default:
        render_move(a, s, pb, pa, 0, 0, out, w, h);
    }
    mosaic(a, s, out, w, h);
}
