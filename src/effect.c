/* Croutons: IFF effect files (ILBM icon, ANIM wipes, CrUD type) and Toaster 4.x project files. */
#include <stdlib.h>
#include <string.h>
#include "effect.h"

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

/* Find chunk id inside [o, end); returns its data offset or 0, *len set. */
static size_t find(const uint8_t *d, size_t o, size_t end, const char *id, uint32_t *len)
{
    while (o + 8 <= end) {
        uint32_t l = be32(d + o + 4);
        if (!memcmp(d + o, id, 4) && o + 8 + l <= end) { *len = l; return o + 8; }
        o += 8 + l + (l & 1);
    }
    return 0;
}

/* Find a FORM of the given type in [o, end). */
static size_t find_form(const uint8_t *d, size_t o, size_t end, const char *type, uint32_t *len)
{
    while (o + 12 <= end) {
        uint32_t l = be32(d + o + 4);
        if (!memcmp(d + o, "FORM", 4) && !memcmp(d + o + 8, type, 4) && o + 8 + l <= end) { *len = l; return o + 8; }
        o += 8 + l + (l & 1);
    }
    return 0;
}

static int unpack(const uint8_t *s, size_t n, uint8_t *out, size_t need, int comp)
{
    size_t i = 0, k = 0;
    if (!comp) {
        if (n < need) return -1;
        memcpy(out, s, need);
        return 0;
    }
    while (k < need && i < n) {     /* a last run may overrun the picture (wipe soft bl-tr !): clipped */
        int c = s[i++];
        size_t r;
        if (c < 128) {
            if (i + c + 1 > n) return -1;
            r = k + c + 1 > need ? need - k : (size_t)c + 1;
            memcpy(out + k, s + i, r); i += c + 1; k += r;
        } else if (c > 128) {
            if (i >= n) return -1;
            r = k + 257 - c > need ? need - k : (size_t)(257 - c);
            memset(out + k, s[i++], r); k += r;
        }
    }
    return k == need ? 0 : -1;
}

/* FORM ILBM body at [o, o+len): w,h,planes and planar rows (row-interleaved, as in the file). */
/* With mask, a masking-1 picture's mask plane is returned there (rb bytes per row, NULL when it has none); with tc,
 * BMHD's transparent colour. */
static uint8_t *ilbm_planes(const uint8_t *d, size_t o, uint32_t len, int *w, int *h, int *np, const uint8_t **cmap,
                            uint32_t *cmaplen, uint8_t **mask, int *tc)
{
    uint32_t bl, bodyl;
    size_t b = find(d, o + 4, o + len, "BMHD", &bl), body = find(d, o + 4, o + len, "BODY", &bodyl), need;
    size_t c = find(d, o + 4, o + len, "CMAP", cmaplen);
    uint8_t *raw;
    int rb;
    if (!b || bl < 20 || !body) return NULL;
    *w = be16(d + b); *h = be16(d + b + 2); *np = d[b + 8];
    if (mask) *mask = NULL;
    if (tc) *tc = be16(d + b + 12);
    if (*np < 1 || *np > 12 || *w <= 0 || *h <= 0) return NULL;
    *cmap = c ? d + c : NULL;
    rb = ((*w + 15) / 16) * 2;
    {
        int rows = *np + (d[b + 9] == 1);       /* masking 1: a mask plane follows each row's planes */
        uint8_t *full;
        need = (size_t)rb * rows * *h;
        full = malloc(need);
        if (full && unpack(d + body, bodyl, full, need, d[b + 10])) { free(full); full = NULL; }
        if (!full || rows == *np) return full;
        raw = malloc((size_t)rb * *np * *h);    /* drop the mask rows */
        if (raw) {
            int y;
            for (y = 0; y < *h; y++) memcpy(raw + (size_t)y * *np * rb, full + (size_t)y * rows * rb, (size_t)*np * rb);
            if (mask && (*mask = malloc((size_t)rb * *h)))
                for (y = 0; y < *h; y++) memcpy(*mask + (size_t)y * rb, full + ((size_t)y * rows + *np) * rb, rb);
        }
        free(full);
        return raw;
    }
}

/* CrUD TAGS: {ULONG id, ULONG data} until 0; ids with 0x10000000 carry a size and that many bytes. */
#define TAGID_SlowFCount 0x80000057u
#define TAGID_MedFCount 0x80000058u
#define TAGID_FastFCount 0x80000059u
#define TAGID_AlgoType 0x80000063u
#define TAGID_AlgoMode 0x80000007u
#define TAGID_AlgoBlock 0x1000001cu
#define TAGID_MainSource 0x80000076u
#define TAGID_Color 0x80000023u
#define TAGID_Overlay 0x80000011u
#define TAGID_Control 0x80000052u

/* What the switcher takes from a tag that holds a number. */
static void tag_apply(struct effect *e, uint32_t id, uint32_t v)
{
    if (id == TAGID_AlgoType) e->algotype = v;
    else if (id == TAGID_MainSource) e->mainsrc = v;
    else if (id == TAGID_Overlay && v) e->overlay = 1;
    else if (id == TAGID_Control) e->ctrl = v;
    else if (id == TAGID_Color) e->maincolor = v;
    else if (id == TAGID_AlgoMode) e->algomode = v;
    else if (id == TAGID_SlowFCount) e->fcount[0] = v;
    else if (id == TAGID_MedFCount) e->fcount[1] = v;
    else if (id == TAGID_FastFCount) e->fcount[2] = v;
}

static void crud_tags(struct effect *e, const uint8_t *d, size_t o, uint32_t len)
{
    size_t end = o + len;
    while (o + 8 <= end) {
        uint32_t id = be32(d + o), v = be32(d + o + 4);
        if (!id) break;
        if (id & 0x10000000u) {
            if (id == TAGID_AlgoBlock && o + 8 + v <= end && !e->algoblob && (e->algoblob = malloc(v))) {
                memcpy(e->algoblob, d + o + 8, v);
                e->algobloblen = v;
            }
            o += 8 + ((v + 3) & ~3u);
            continue;
        }
        tag_apply(e, id, v);
        o += 8;
    }
}

static void crud_type(struct effect *e, const uint8_t *d, size_t n)
{
    uint32_t l, tl, gl;
    size_t f = find_form(d, 0, n, "CrUD", &l), t, g;
    static const struct { const char *id; int type; } types[] = {
        { "ANIM", FX_ANIM }, { "ALGO", FX_ALGO }, { "ILBM", FX_ILBM }, { "MAIN", FX_MAIN }, { "CTRL", FX_CTRL },
        { "REXX", FX_REXX } };
    unsigned i;
    e->type = FX_OTHER;
    if (!f || !(t = find(d, f + 4, f + l, "TYPE", &tl)) || tl < 4) return;
    for (i = 0; i < sizeof(types) / sizeof(types[0]); i++)
        if (!memcmp(d + t, types[i].id, 4)) e->type = types[i].type;
    if ((g = find(d, f + 4, f + l, "TAGS", &gl))) {
        crud_tags(e, d, g, gl);
        if (!e->tags && (e->tags = malloc(gl))) {
            memcpy(e->tags, d + g, gl);
            e->tagslen = gl;
        }
    }
}

static void find_icon(struct effect *e, const uint8_t *d, size_t n)
{
    uint32_t l, cl;
    size_t f;
    int w, h, np, x, y, p;
    const uint8_t *cmap;
    uint8_t *raw;

    /* the crouton icon: the top-level FORM ILBM that is 80x50 */
    for (f = 0; f + 12 <= n; f += 8 + l + (l & 1)) {
        l = be32(d + f + 4);
        if (f + 8 + l > n) break;
        if (memcmp(d + f, "FORM", 4) || memcmp(d + f + 8, "ILBM", 4)) continue;
        if (!(raw = ilbm_planes(d, f + 8, l, &w, &h, &np, &cmap, &cl, NULL, NULL))) continue;
        if (w == ICON_W && h == ICON_H && cmap) {
            int rb = ((w + 15) / 16) * 2;
            for (y = 0; y < h; y++)
                for (x = 0; x < w; x++) {
                    int v = 0;
                    for (p = 0; p < np; p++)
                        v |= ((raw[(size_t)(y * np + p) * rb + x / 8] >> (7 - x % 8)) & 1) << p;
                    e->icon[y * w + x] = (uint32_t)v * 3 + 2 < cl ?
                        (uint32_t)cmap[v * 3] << 16 | cmap[v * 3 + 1] << 8 | cmap[v * 3 + 2] : 0;
                }
            e->has_icon = 1;
            free(raw);
            break;
        }
        free(raw);
    }
}

int effect_open(struct effect *e, const char *name, const uint8_t *d, size_t n)
{
    memset(e, 0, sizeof(*e));
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->file = d;
    e->filelen = n;
    crud_type(e, d, n);
    find_icon(e, d, n);
    return 0;
}

void effect_merge_icon(struct effect *e, const uint8_t *ib, size_t n)
{
    if (e->type == FX_OTHER) crud_type(e, ib, n);
    if (!e->has_icon) find_icon(e, ib, n);
}

/* ANIM op 5: byte vertical delta, one list per plane. */
static void op5(uint8_t **pl, int np, const uint8_t *dl, uint32_t len, int rb, int h)
{
    int p;
    if (len < 64) return;
    for (p = 0; p < np && p < 8; p++) {
        uint32_t o = be32(dl + 4 * p);
        int col;
        if (!o) continue;
        for (col = 0; col < rb; col++) {
            int ops, row = 0;
            if (o >= len) return;
            ops = dl[o++];
            while (ops--) {
                int op;
                if (o >= len) return;
                op = dl[o++];
                if (op == 0) {
                    int cnt, val;
                    if (o + 2 > len) return;
                    cnt = dl[o]; val = dl[o + 1]; o += 2;
                    while (cnt-- && row < h) pl[p][(size_t)row++ * rb + col] = val;
                } else if (op & 0x80) {
                    int cnt = op & 0x7f;
                    while (cnt-- && o < len) { if (row < h) pl[p][(size_t)row * rb + col] = dl[o]; o++; row++; }
                } else {
                    row += op;
                }
            }
        }
    }
}

/* NewTek's op 7 (switcher UpDatePic -> one handler per ANHD bits value): every plane is a run list read from one
 * stream that continues across planes. A run is {count, skip, data}: skip moves the write position by skip items
 * (signed), a positive count copies count items, a negative count repeats one item -count times, 0 ends the plane.
 * bits 0/1/16/17: word counts and skips, items word/long/word/long, 16 and 17 step down a row per item;
 * bits 64/80: byte counts, big-endian byte skips, byte items, 80 steps down; a byte count of -1 only skips. */
static size_t op7_plane(uint8_t *pl, int rb, int h, const uint8_t *d, size_t o, size_t end, int bits)
{
    long pos = 0, size = (long)rb * h;
    int unit = bits == 1 || bits == 17 ? 4 : bits == 64 || bits == 80 ? 1 : 2;
    int vert = bits == 16 || bits == 17 || bits == 80;
    long step = vert ? rb : unit;
    for (;;) {
        int cnt, i, n;
        long skip;
        if (unit == 1) {
            if (o + 1 > end) return o;
            cnt = (int8_t)d[o++];
            if (!cnt) return o;
            if (o + 2 > end) return o;
            skip = (int16_t)be16(d + o); o += 2;
            pos += skip;
            if (cnt == -1) continue;
        } else {
            if (o + 2 > end) return o;
            cnt = (int16_t)be16(d + o); o += 2;
            if (!cnt) return o;
            if (o + 2 > end) return o;
            skip = (int16_t)be16(d + o); o += 2;
            pos += skip * unit;
            if (cnt < 0 && !(uint16_t)~cnt) continue;   /* -1: skip only */
        }
        if (cnt > 0) {
            for (i = 0; i < cnt; i++, pos += step, o += unit) {
                if (o + unit > end) return end;
                if (pos >= 0 && pos + unit <= size) memcpy(pl + pos, d + o, unit);
            }
        } else {
            const uint8_t *v;
            n = -cnt;
            if (o + unit > end) return end;
            v = d + o; o += unit;
            for (i = 0; i < n; i++, pos += step)
                if (pos >= 0 && pos + unit <= size) memcpy(pl + pos, v, unit);
        }
    }
}

/* NewTek's op 8 (switcher 0x31598): a 12-byte header per plane {offset in words, size in words, control}, then the
 * planes' data in one stream. Control 0 leaves the plane alone. Otherwise the plane's region [offset, offset+size)
 * is filled from its end backwards: one word if control is odd, (control low word / 2) longwords, then
 * (control high word) 52-byte blocks (68k movem of 13 registers), each piece keeping its own byte order. */
static void op8(uint8_t **pl, int np, const uint8_t *d, uint32_t len, int rb, int h)
{
    size_t data = (size_t)12 * np, size = (size_t)rb * h;
    int p;
    if (len < data) return;
    for (p = 0; p < np && p < 8; p++) {
        uint32_t off = be32(d + 12 * p), words = be32(d + 12 * p + 4), ctl = be32(d + 12 * p + 8), i;
        size_t end, q = data, bytes = (size_t)words * 2;
        if (!ctl) continue;
        end = (size_t)off * 2 + bytes;
        if (data + bytes > len) return;
        data += bytes;
        if (end > size) continue;
        if (ctl & 1) { if (end < 2) continue; end -= 2; memcpy(pl[p] + end, d + q, 2); q += 2; }
        for (i = 0; i < (ctl & 0xfffe) / 2; i++) { if (end < 4) break; end -= 4; memcpy(pl[p] + end, d + q, 4); q += 4; }
        for (i = 0; i < ctl >> 16; i++) { if (end < 52) break; end -= 52; memcpy(pl[p] + end, d + q, 52); q += 52; }
    }
}

/* Overlays: an all-black palette makes the values key levels (0 = black, the top value = video, between = soft
 * edge: binoculars); otherwise transparent colour tcol shows video and the rest is artwork in CMAP colours. */
static int overlay_key(struct effect *e, const uint8_t *cmap, uint32_t cl, int np, int tcol)
{
    uint32_t k, lit = 0, top = 0;
    for (k = 0; k < 256; k++)
        e->color[k] = cmap && k * 3 + 2 < cl ? (uint32_t)cmap[k * 3] << 16 | cmap[k * 3 + 1] << 8 | cmap[k * 3 + 2] : 0;
    for (k = 0; k * 3 + 2 < cl && k < 256; k++) {
        lit |= e->color[k];
        top = cmap[k * 3] > top ? cmap[k * 3] : top;
        top = cmap[k * 3 + 1] > top ? cmap[k * 3 + 1] : top;
        top = cmap[k * 3 + 2] > top ? cmap[k * 3 + 2] : top;
    }
    e->keyonly = !lit;
    e->keymax = (1 << (np > 8 ? 8 : np)) - 1;
    e->tcol = tcol;
    return lit && top <= 2 ? -1 : 0;   /* values 0..2 in the palette: data the card used (raster shifts), not colours */
}

/* A key-level overlay that never reaches full video (kinescope's 0/1 of 3) is data for the card too. */
static int overlay_keyable(const struct effect *e)
{
    int f, x, y;
    if (!e->keyonly) return 1;
    for (f = 0; f < e->nframes; f++)
        for (y = 0; y < e->h; y += 2)
            for (x = 0; x < e->w; x++)
                if (effect_mask(e, f, x, y) == e->keymax) return 1;
    return 0;
}

/* FX_ILBM: the full-size greyscale picture (not the 80x50 icon) becomes the wipe map. */
static int wipemap_frames(struct effect *e)
{
    const uint8_t *d = e->file;
    uint32_t l, cl;
    size_t f;
    int w, h, np, x, y, p, tc, k;
    const uint8_t *cmap;
    uint8_t *raw, *mask;
    for (f = 0; f + 12 <= e->filelen; f += 8 + l + (l & 1)) {
        int rb;
        l = be32(d + f + 4);
        if (f + 8 + l > e->filelen) break;
        if (memcmp(d + f, "FORM", 4) || memcmp(d + f + 8, "ILBM", 4)) continue;
        if (!(raw = ilbm_planes(d, f + 8, l, &w, &h, &np, &cmap, &cl, &mask, &tc))) continue;
        if (w == ICON_W && h == ICON_H) { free(raw); free(mask); continue; }
        if (np > 8) { free(raw); free(mask); return -1; }   /* 10/12-plane bars: two interleaved pictures, not yet */
        rb = ((w + 15) / 16) * 2;
        if (e->overlay) {
            if (!(e->masks = malloc((size_t)w * h))) { free(raw); free(mask); return -1; }
            for (y = 0; y < h; y++)
                for (x = 0; x < w; x++) {
                    int v = 0;
                    for (p = 0; p < np; p++)
                        v |= ((raw[(size_t)(y * np + p) * rb + x / 8] >> (7 - x % 8)) & 1) << p;
                    e->masks[(size_t)y * w + x] = mask && !((mask[(size_t)y * rb + x / 8] >> (7 - x % 8)) & 1) ? 255 : v;
                }
            k = overlay_key(e, cmap, cl, np, mask ? 255 : tc);
            free(raw);
            free(mask);
            e->w = w; e->h = h; e->nframes = 1; e->bpp = 8; e->stride = w;
            if (k || !overlay_keyable(e)) { effect_free_frames(e); return -1; }
            return 0;
        }
        free(mask);
        {
            /* a mostly grey palette (a few stray colours at the end) is a wipe map; bigger than a frame scrolls */
            uint32_t k, grey = 0, ncol = cl / 3;
            for (k = 0; k < ncol; k++)
                grey += cmap[k * 3] == cmap[k * 3 + 1] && cmap[k * 3 + 1] == cmap[k * 3 + 2];
            uint32_t black = 0;
            for (k = 0; k < ncol; k++) black += !cmap[k * 3] && !cmap[k * 3 + 1] && !cmap[k * 3 + 2];
            e->wipemap = h > FRAME_H + 8 ? PIC_SCROLLV : w > FRAME_W + 8 ? PIC_SCROLLH :
                         np == 8 || (ncol && grey * 10 >= ncol * 9 && np >= 4 && black < ncol) ? PIC_MAP : PIC_NONE;
            if (e->wipemap == PIC_NONE) { free(raw); return -1; }   /* overlays: not yet */
            for (k = 0; k < 256; k++)
                e->color[k] = k * 3 + 2 < cl ? (uint32_t)cmap[k * 3] << 16 | cmap[k * 3 + 1] << 8 | cmap[k * 3 + 2] : 0;
        }
        if (!(e->masks = malloc((size_t)w * h))) { free(raw); return -1; }
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                int v = 0;
                for (p = 0; p < np; p++)
                    v |= ((raw[(size_t)(y * np + p) * rb + x / 8] >> (7 - x % 8)) & 1) << p;
                /* map level: the index for 8-plane maps (colours there are only for show), the grey otherwise */
                e->masks[(size_t)y * w + x] = e->wipemap == PIC_MAP && np < 8 && cmap && (uint32_t)v * 3 < cl ? cmap[v * 3] : v;
            }
        free(raw);
        e->w = w; e->h = h; e->nframes = 1; e->bpp = 8; e->stride = w;
        return 0;
    }
    return -1;
}

int effect_frames(struct effect *e)
{
    const uint8_t *d = e->file;
    uint32_t l, fl;
    size_t a, f;
    int w = 0, h = 0, np = 0, rb = 0, k = 0, n = 0, p, cap = 0, x, y, s = 0, tc0 = 0;
    uint8_t *buf[4][12], *raw = NULL;
    int nbuf = 2;
    const uint8_t *cmap;
    uint32_t cl;

    if (e->masks || e->algo) return 0;
    if (e->type == FX_ALGO) {
        if (!e->algoblob || !(e->algo = malloc(sizeof(*e->algo)))) return -1;
        if (algofx_parse(e->algo, e->algotype, e->algomode, e->algoblob, e->algobloblen)) {
            free(e->algo);
            e->algo = NULL;
            return -1;
        }
        return 0;
    }
    /* the data decides (some croutons' CrUD TYPE says ANIM for a wipe map, or ILBM for an ANIM) */
    if (e->type != FX_ANIM && e->type != FX_ILBM) return -1;
    if (!(a = find_form(d, 0, e->filelen, "ANIM", &l))) return wipemap_frames(e);
    memset(buf, 0, sizeof(buf));
    for (f = a + 4; f + 12 <= a + l; f += 8 + fl + (fl & 1)) {
        fl = be32(d + f + 4);
        if (memcmp(d + f, "FORM", 4) || memcmp(d + f + 8, "ILBM", 4)) continue;
        if (!n) {           /* first frame: a full ILBM */
            if (!(raw = ilbm_planes(d, f + 8, fl, &w, &h, &np, &cmap, &cl, NULL, &tc0)) || np > 12) goto fail;
            rb = ((w + 15) / 16) * 2;
            for (p = 0; p < 256; p++)
                e->color[p] = cmap && (uint32_t)p * 3 + 2 < cl ?
                    (uint32_t)cmap[p * 3] << 16 | cmap[p * 3 + 1] << 8 | cmap[p * 3 + 2] : 0;
            e->bpp = np <= 2 ? 2 : np <= 4 ? 4 : 8;
            {   /* interleave of the first delta: number of buffers (0 = 2) */
                size_t g = f + 8 + fl + (fl & 1), a1, a2;
                uint32_t gl, al;
                if (g + 12 <= a + l && (gl = be32(d + g + 4)) && (a1 = find(d, g + 12, g + 8 + gl, "ANHD", &al)) && al >= 24) {
                    nbuf = d[a1 + 18] ? d[a1 + 18] : 2;
                    if (nbuf > 4) nbuf = 4;
                }
                (void)a2;
            }
            for (s = 0; s < nbuf; s++)
                for (p = 0; p < np; p++) {
                    if (!(buf[s][p] = malloc((size_t)rb * h))) goto fail;
                    for (y = 0; y < h; y++) memcpy(buf[s][p] + (size_t)y * rb, raw + (size_t)(y * np + p) * rb, rb);
                }
            free(raw);
            raw = NULL;
            s = 0;
        } else {
            uint32_t al, dlen;
            size_t ah = find(d, f + 12, f + 8 + fl, "ANHD", &al), dl = find(d, f + 12, f + 8 + fl, "DLTA", &dlen);
            if (ah && al >= 24 && d[ah] == 0) {     /* op 0: a whole frame as an ILBM BODY */
                int w2, h2, np2;
                k++;
                s = k % nbuf;
                if (!(raw = ilbm_planes(d, f + 8, fl, &w2, &h2, &np2, &cmap, &cl, NULL, NULL)) || w2 != w || h2 != h ||
                    np2 != np)
                    goto fail;
                for (p = 0; p < np; p++)
                    for (y = 0; y < h; y++) memcpy(buf[s][p] + (size_t)y * rb, raw + (size_t)(y * np + p) * rb, rb);
                free(raw);
                raw = NULL;
                goto store;
            }
            if (!ah || al < 24 || !dl) continue;
            k++;
            s = k % nbuf;           /* a delta applies to the frame nbuf back */
            if (k < nbuf)           /* the first nbuf-1 deltas build on the previous frame */
                for (p = 0; p < np; p++) memcpy(buf[s][p], buf[(k - 1) % nbuf][p], (size_t)rb * h);
            if (d[ah] == 5) {
                op5(buf[s], np, d + dl, dlen, rb, h);
            } else if (d[ah] == 7) {
                uint32_t bits = be32(d + ah + 20);
                size_t q = dl;
                if (bits != 0 && bits != 1 && bits != 16 && bits != 17 && bits != 64 && bits != 80) goto fail;
                if (dlen > 2)       /* a 2-byte DLTA is an unchanged frame */
                    for (p = 0; p < np; p++) q = op7_plane(buf[s][p], rb, h, d, q, dl + dlen, bits);
            } else if (d[ah] == 8) {
                op8(buf[s], np, d + dl, dlen, rb, h);
            } else {
                goto fail;
            }
        }
store:
        {
            int stride = (w * e->bpp + 7) / 8;
            uint8_t *m;
            if (n == cap) {
                m = realloc(e->masks, (size_t)(cap = cap ? cap * 2 : 32) * h * stride);
                if (!m) goto fail;
                e->masks = m;
            }
            m = e->masks + (size_t)n * h * stride;
            memset(m, 0, (size_t)h * stride);
            for (y = 0; y < h; y++)
                for (x = 0; x < w; x++) {
                    int v = 0;
                    for (p = 0; p < np && p < 8; p++)
                        v |= ((buf[s][p][(size_t)y * rb + x / 8] >> (7 - x % 8)) & 1) << p;
                    if (e->bpp == 8) m[(size_t)y * stride + x] = v;
                    else if (e->bpp == 4) m[(size_t)y * stride + x / 2] |= (v & 15) << (x & 1 ? 0 : 4);
                    else m[(size_t)y * stride + x / 4] |= (v & 3) << (6 - 2 * (x & 3));
                }
        }
        n++;
    }
    e->w = w; e->h = h; e->nframes = n; e->stride = (w * e->bpp + 7) / 8;
    if (n && e->overlay) {         /* the first frame's palette is in e->color */
        uint8_t cm[768];
        for (p = 0; p < 256; p++) {
            cm[p * 3] = e->color[p] >> 16;
            cm[p * 3 + 1] = e->color[p] >> 8;
            cm[p * 3 + 2] = e->color[p];
        }
        e->nframes = n;
        if (overlay_key(e, cm, 3u << (np > 8 ? 8 : np), np, tc0) || !overlay_keyable(e)) n = 0;
    } else if (n) {
        static long cnt[2][256];
        int f2, v, a = 0, b = 0, nv = 1 << (np > 8 ? 8 : np), ramp = nv >= 8;
        memset(cnt, 0, sizeof(cnt));
        for (f2 = 0; f2 < 2; f2++)
            for (y = 0; y < h; y += 2)
                for (x = 0; x < w; x += 2)
                    cnt[f2][effect_mask(e, f2 ? n - 1 : 0, x, y)]++;
        for (v = 1; v < nv; v++) {
            if (cnt[0][v] > cnt[0][a]) a = v;
            if (cnt[1][v] > cnt[1][b]) b = v;
        }
        for (v = 0; v < 256; v++) e->role[v] = FXR_COLOR;
        e->role[a] = FXR_A;
        if (b != a) e->role[b] = FXR_B;
        /* soft wipe: a rising grey CMAP; each value's grey is its share of the destination */
        for (v = 0; v < nv && ramp; v++) {
            uint32_t c = e->color[v];
            if (((c >> 16) & 255) != ((c >> 8) & 255) || ((c >> 8) & 255) != (c & 255) ||
                (v && (c & 255) < (e->color[v - 1] & 255)))
                ramp = 0;
        }
        e->soft = ramp && a != b;
        if (e->soft)
            for (v = 0; v < nv; v++) {
                int g = e->color[v] & 255, ga = e->color[a] & 255, gb = e->color[b] & 255;
                e->level[v] = gb == ga ? (v == b ? 255 : 0) : (uint8_t)(g <= (ga < gb ? ga : gb) ? (ga < gb ? 0 : 255) :
                              g >= (ga > gb ? ga : gb) ? (ga < gb ? 255 : 0) : (g - ga) * 255 / (gb - ga));
            }
    }
    for (k = 0; k < 4; k++) for (p = 0; p < 12; p++) free(buf[k][p]);
    if (!n) effect_free_frames(e);
    return n ? 0 : -1;
fail:
    free(raw);
    for (k = 0; k < 4; k++) for (p = 0; p < 12; p++) free(buf[k][p]);
    effect_free_frames(e);
    return -1;
}

void effect_note_path(struct effect *e, const char *path)
{
    static const char dir[] = "overlays/";
    size_t i, k;
    strncpy(e->path, path, sizeof(e->path) - 1);
    e->path[sizeof(e->path) - 1] = 0;
    for (i = 0; path[i]; i++) {
        for (k = 0; dir[k] && path[i + k] && (path[i + k] | 0x20) == dir[k]; k++)
            ;
        if (!dir[k] && (!i || path[i - 1] == '/' || path[i - 1] == ':')) e->overlay = 1;
    }
}

int effect_main_slot(const struct effect *e)
{
    int b;
    if (e->type != FX_MAIN) return -1;
    for (b = 0; b < 7; b++)
        if (e->mainsrc & 1 << b) return b;
    return -1;
}

void effect_close(struct effect *e)
{
    effect_free_frames(e);
    free(e->entry);
    e->entry = NULL;
    free(e->algoblob);
    e->algoblob = NULL;
    free(e->tags);
    e->tags = NULL;
}

void effect_free_frames(struct effect *e)
{
    free(e->algo);
    e->algo = NULL;
    free(e->masks);
    e->masks = NULL;
    e->nframes = 0;
    e->wipemap = 0;
}

int effect_picture(const struct effect *e, int fx, int fy, int t, int tmax, int *level, uint32_t *rgb)
{
    int v;
    if (e->wipemap == PIC_MAP) {
        *level = wipemap_level(e->masks[(size_t)(fy * e->h / FRAME_H) * e->w + fx * e->w / FRAME_W], t, tmax);
        return FXR_A;
    }
    if (e->wipemap == PIC_SCROLLV) {
        long py = fy - FRAME_H + (long)t * e->h / tmax;
        if (py < 0) return FXR_A;
        if (py >= e->h) return FXR_B;
        v = e->masks[(size_t)py * e->w + fx * e->w / FRAME_W];
    } else {
        long px = fx - FRAME_W + (long)t * e->w / tmax;
        if (px < 0) return FXR_A;
        if (px >= e->w) return FXR_B;
        v = e->masks[(size_t)(fy * e->h / FRAME_H) * e->w + px];
    }
    if (v == 0) return FXR_A;
    if (v == 1) return FXR_B;
    *rgb = e->color[v];
    return FXR_COLOR;
}

int effect_overlay_px(const struct effect *e, int f, int fx, int fy, uint32_t matte, uint32_t *rgb)
{
    int v = effect_mask(e, f, fx * e->w / FRAME_W, fy * e->h / FRAME_H);
    *rgb = e->keyonly ? matte : e->color[v];
    if (e->keyonly) return 255 - v * 255 / (e->keymax ? e->keymax : 1);
    return v == e->tcol ? 0 : 255;
}

int effect_ticks(const struct effect *e, int speed, int dflt)
{
    static const int fields[3] = { 3, 2, 1 };
    int v = speed >= 0 && speed < 3 ? e->fcount[speed] : 0;
    if (e->overlay) return v ? v * 50 / 30 : dflt;
    if (e->type == FX_ANIM && e->nframes && !e->wipemap)
        return e->nframes * (v ? v : fields[speed]) * 50 / 60;
    return v ? v * 50 / 30 : dflt;
}

int project_entries(const uint8_t *d, size_t n, char names[][256], size_t off[], int max)
{
    size_t o;
    int k = 0;
    if (n < 32 || memcmp(d, "Project", 8) || be32(d + 8)) return -1;
    o = 32 + be32(d + 28);
    while (o + 4 <= n && k < max) {
        uint32_t size = be32(d + o), nl;
        if (!size) break;
        if (o + 4 + size > n || size < 30) return k;
        nl = be16(d + o + 28);
        if (nl && 30 + nl <= size + 4) {
            size_t len = 0;
            while (len < nl && len < 255 && d[o + 30 + len]) len++;
            memcpy(names[k], d + o + 30, len);
            names[k][len] = 0;
            if (off) off[k] = o;
            k++;
        }
        o += 4 + size;
    }
    return k;
}

size_t project_header(const uint8_t *d, size_t n)
{
    size_t h;
    if (n < 32 || memcmp(d, "Project", 8) || be32(d + 8)) return 0;
    h = 32 + be32(d + 28);
    return h <= n ? h : 0;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

uint8_t *project_entry_new(const char *path, const char *type, size_t *len)
{
    size_t nl = strlen(path) + 1, size;
    uint8_t *e;
    nl += nl & 1;
    size = 26 + nl + 4;                     /* after the size long: type, version, 4 pads, name size, name, no tags */
    if (!(e = calloc(1, 4 + size))) return NULL;
    put32(e, size);
    memcpy(e + 4, type, 4);
    e[28] = nl >> 8;
    e[29] = nl;
    memcpy(e + 30, path, strlen(path));
    *len = 4 + size;
    return e;
}

#define TAGID_CtlSpeed 0x80f70001u
#define TAGID_CtlHold 0x80f70002u

/* Offset of the tags in a project entry (after its name), 0 if it is too short. */
static size_t entry_tags(const uint8_t *e, size_t len)
{
    size_t o;
    if (len < 30) return 0;
    o = 30 + be16(e + 28);
    return o <= len ? o : 0;
}

void effect_entry_controls(struct effect *e)
{
    size_t o = e->entry ? entry_tags(e->entry, e->entrylen) : 0;
    e->ctl_speed = e->ctl_hold = -1;
    while (o && o + 8 <= e->entrylen) {
        uint32_t id = be32(e->entry + o), v = be32(e->entry + o + 4);
        if (!id) break;
        if (id == TAGID_CtlSpeed && v < 3) e->ctl_speed = v;
        else if (id == TAGID_CtlHold && v < 100000) e->ctl_hold = v;
        else if (!(id & 0x10000000u)) tag_apply(e, id, v);      /* the project's tags go before the crouton's own */
        o += 8 + (id & 0x10000000u ? (v + 3) & ~3u : 0);
    }
}

/* The tag id in the list at d + o: its value, or its size with its bytes at *data. */
static int tag_find(const uint8_t *d, size_t o, size_t end, uint32_t id, uint32_t *v, const uint8_t **data)
{
    while (o + 8 <= end) {
        uint32_t t = be32(d + o), x = be32(d + o + 4);
        size_t l = 8 + (t & 0x10000000u ? ((size_t)x + 3) & ~(size_t)3 : 0);
        if (!t || l > end - o) break;
        if (t == id) {
            *v = x;
            if (data) *data = d + o + 8;
            return 1;
        }
        o += l;
    }
    return 0;
}

int effect_tag(const struct effect *e, uint32_t id, uint32_t *v, const uint8_t **data)
{
    size_t o = e->entry ? entry_tags(e->entry, e->entrylen) : 0;
    if (o && tag_find(e->entry, o, e->entrylen, id, v, data)) return 1;
    return e->tags && tag_find(e->tags, 0, e->tagslen, id, v, data);
}

int effect_set_tag(struct effect *e, uint32_t id, uint32_t v, const void *data)
{
    size_t t = e->entry ? entry_tags(e->entry, e->entrylen) : 0, o = t, n, add = 8;
    uint8_t *d;
    if (id & 0x10000000u) add += ((size_t)v + 3) & ~(size_t)3;
    if (!t || !id || !(d = calloc(1, e->entrylen + add + 4))) return -1;
    memcpy(d, e->entry, t);
    n = t;
    while (o + 8 <= e->entrylen) {      /* the other tags, as they were */
        uint32_t i = be32(e->entry + o), x = be32(e->entry + o + 4);
        size_t l = 8 + (i & 0x10000000u ? ((size_t)x + 3) & ~(size_t)3 : 0);
        if (!i || l > e->entrylen - o) break;
        if (i != id) {
            memcpy(d + n, e->entry + o, l);
            n += l;
        }
        o += l;
    }
    put32(d + n, id);
    put32(d + n + 4, v);
    if (id & 0x10000000u) memcpy(d + n + 8, data, v);
    else tag_apply(e, id, v);
    n += add;
    put32(d + n, 0);
    n += 4;
    put32(d, n - 4);
    free(e->entry);
    e->entry = d;
    e->entrylen = n;
    return 0;
}

int effect_set_controls(struct effect *e, int speed, int hold)
{
    size_t t = e->entry ? entry_tags(e->entry, e->entrylen) : 0, o = t, n = 0;
    uint8_t *d;
    if (!t || !(d = malloc(e->entrylen + 20))) return -1;
    memcpy(d, e->entry, t);
    n = t;
    while (o + 8 <= e->entrylen) {      /* the other tags, as they were */
        uint32_t id = be32(e->entry + o), v = be32(e->entry + o + 4);
        size_t l = 8 + (id & 0x10000000u ? (v + 3) & ~3u : 0);
        if (!id || o + l > e->entrylen) break;
        if (id != TAGID_CtlSpeed && id != TAGID_CtlHold) {
            memcpy(d + n, e->entry + o, l);
            n += l;
        }
        o += l;
    }
    if (speed >= 0) { put32(d + n, TAGID_CtlSpeed); put32(d + n + 4, speed); n += 8; }
    if (hold >= 0) { put32(d + n, TAGID_CtlHold); put32(d + n + 4, hold); n += 8; }
    put32(d + n, 0);
    n += 4;
    put32(d, n - 4);
    free(e->entry);
    e->entry = d;
    e->entrylen = n;
    e->ctl_speed = speed;
    e->ctl_hold = hold;
    return 0;
}

int project_build(const uint8_t *hdr, size_t hdrlen, const struct effect *fx, int nfx, uint8_t **out, size_t *n)
{
    static const uint8_t blank[44] = { 'P', 'r', 'o', 'j', 'e', 'c', 't', 0, [31] = 12, [32] = 0x84, [35] = 0x77 };
    size_t total, o;
    int i;
    uint8_t *d;
    if (!hdr || !hdrlen) { hdr = blank; hdrlen = sizeof(blank); }
    total = hdrlen + 4;
    for (i = 0; i < nfx; i++) total += fx[i].entrylen;
    if (!(d = malloc(total))) return -1;
    memcpy(d, hdr, hdrlen);
    o = hdrlen;
    for (i = 0; i < nfx; i++) {
        if (fx[i].entry) memcpy(d + o, fx[i].entry, fx[i].entrylen);
        o += fx[i].entrylen;
    }
    put32(d + o, 0);
    *out = d;
    *n = total;
    return 0;
}
