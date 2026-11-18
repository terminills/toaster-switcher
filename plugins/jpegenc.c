/* A baseline JPEG encoder (see jpegenc.h): YCbCr 4:2:0, the quantisation tables of the standard's annex K at
 * quality 80, its Huffman tables, the AAN forward DCT in floating point. */
#include <string.h>
#include "jpegenc.h"

#define QUALITY 80

static const uint8_t zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21,
    28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54,
    47, 55, 62, 63
};
static const uint8_t basequant[2][64] = { {
    16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51,
    87, 80, 62, 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103, 99
}, {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99
} };
/* the Huffman tables: how many codes of each length, then the values in the order of their codes */
static const uint8_t dcbits[2][16] = {
    { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 }, { 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 }
};
static const uint8_t dcvals[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
static const uint8_t acbits[2][16] = {
    { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d }, { 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 }
};
static const uint8_t acvals[2][162] = { {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14,
    0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09,
    0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
    0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65,
    0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
    0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9,
    0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca,
    0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
    0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa
}, {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32,
    0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16,
    0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64,
    0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86,
    0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8,
    0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
    0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa
} };

struct huff {
    uint16_t code[256];
    uint8_t len[256];
};

struct writer {
    uint8_t *p;
    uint32_t acc;               /* bits not written yet, the newest lowest */
    int nacc;
};

static void put(struct writer *w, unsigned code, int len)
{
    w->acc = w->acc << len | code;
    w->nacc += len;
    while (w->nacc >= 8) {
        uint8_t b = w->acc >> (w->nacc - 8);
        *w->p++ = b;
        if (b == 0xff) *w->p++ = 0;     /* not a marker */
        w->nacc -= 8;
    }
}

static void bytes(struct writer *w, const void *data, size_t n)
{
    memcpy(w->p, data, n);
    w->p += n;
}

static void marker(struct writer *w, int m, int len)
{
    uint8_t h[4] = { 0xff, (uint8_t)m, (uint8_t)((len + 2) >> 8), (uint8_t)(len + 2) };
    bytes(w, h, 4);
}

static void make_huff(struct huff *h, const uint8_t *bits, const uint8_t *vals)
{
    unsigned code = 0;
    int l, i, k = 0;
    memset(h, 0, sizeof(*h));
    for (l = 1; l <= 16; l++, code <<= 1)
        for (i = 0; i < bits[l - 1]; i++, k++) {
            h->code[vals[k]] = code++;
            h->len[vals[k]] = l;
        }
}

/* 8 values, step apart: the AAN DCT, which leaves each coefficient scaled by a factor quantising takes out. */
static void dct8(float *d, int step)
{
    float t0 = d[0] + d[7 * step], t7 = d[0] - d[7 * step], t1 = d[step] + d[6 * step], t6 = d[step] - d[6 * step];
    float t2 = d[2 * step] + d[5 * step], t5 = d[2 * step] - d[5 * step];
    float t3 = d[3 * step] + d[4 * step], t4 = d[3 * step] - d[4 * step];
    float t10 = t0 + t3, t13 = t0 - t3, t11 = t1 + t2, t12 = t1 - t2, z1, z2, z3, z4, z5, z11, z13;
    d[0] = t10 + t11;
    d[4 * step] = t10 - t11;
    z1 = (t12 + t13) * 0.707106781f;
    d[2 * step] = t13 + z1;
    d[6 * step] = t13 - z1;
    t10 = t4 + t5;
    t11 = t5 + t6;
    t12 = t6 + t7;
    z5 = (t10 - t12) * 0.382683433f;
    z2 = t10 * 0.541196100f + z5;
    z4 = t12 * 1.306562965f + z5;
    z3 = t11 * 0.707106781f;
    z11 = t7 + z3;
    z13 = t7 - z3;
    d[5 * step] = z13 + z2;
    d[3 * step] = z13 - z2;
    d[step] = z11 + z4;
    d[7 * step] = z11 - z4;
}

/* One block: transformed, quantised (by scale, which has 1 / (quantiser * the DCT's factor)), and its codes. Returns
 * its DC value, for the next block of the component. */
static int block(struct writer *w, float *d, const float *scale, int lastdc, const struct huff *dc,
                 const struct huff *ac)
{
    int q[64], i, run = 0, last = 0, diff, mag, n;
    for (i = 0; i < 8; i++) dct8(d + i * 8, 1);
    for (i = 0; i < 8; i++) dct8(d + i, 8);
    for (i = 0; i < 64; i++) {
        float v = d[zigzag[i]] * scale[zigzag[i]];
        q[i] = (int)(v < 0 ? v - 0.5f : v + 0.5f);
        if (q[i]) last = i;
    }
    diff = q[0] - lastdc;
    mag = diff < 0 ? -diff : diff;
    for (n = 0; mag >> n; n++)
        ;
    put(w, dc->code[n], dc->len[n]);
    if (n) put(w, (diff < 0 ? diff - 1 : diff) & ((1 << n) - 1), n);
    for (i = 1; i <= last; i++) {
        if (!q[i]) {
            run++;
            continue;
        }
        for (; run > 15; run -= 16) put(w, ac->code[0xf0], ac->len[0xf0]);
        mag = q[i] < 0 ? -q[i] : q[i];
        for (n = 0; mag >> n; n++)
            ;
        put(w, ac->code[run << 4 | n], ac->len[run << 4 | n]);
        put(w, (q[i] < 0 ? q[i] - 1 : q[i]) & ((1 << n) - 1), n);
        run = 0;
    }
    if (last != 63) put(w, ac->code[0], ac->len[0]);
    return q[0];
}

size_t jpeg_bound(int w, int h)
{
    /* at most 16 bits a coefficient and their stuffing, for the pictures' blocks and half as many of colour */
    return 1024 + (size_t)((w + 15) / 16) * ((h + 15) / 16) * 6 * 64 * 4;
}

size_t jpeg_encode(const uint32_t *rgb, int w, int h, uint8_t *out)
{
    static const float aan[8] = { 1.0f, 1.387039845f, 1.306562965f, 1.175875602f, 1.0f, 0.785694958f, 0.541196100f,
                                  0.275899379f };
    static const uint8_t jfif[] = { 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0 };
    static const uint8_t scan[] = { 3, 1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0 };
    struct writer wr = { out, 0, 0 };
    struct huff hdc[2], hac[2];
    uint8_t quant[2][64], sof[15];
    float scale[2][64];
    int t, i, mx, my, x, y, dcy = 0, dcb = 0, dcr = 0;

    for (t = 0; t < 2; t++) {
        for (i = 0; i < 64; i++) {
            int v = (basequant[t][i] * (200 - 2 * QUALITY) + 50) / 100;
            quant[t][i] = v < 1 ? 1 : v > 255 ? 255 : v;
            scale[t][i] = 1.0f / (quant[t][i] * aan[i / 8] * aan[i % 8] * 8.0f);
        }
        make_huff(&hdc[t], dcbits[t], dcvals);
        make_huff(&hac[t], acbits[t], acvals[t]);
    }
    *wr.p++ = 0xff;
    *wr.p++ = 0xd8;
    marker(&wr, 0xe0, sizeof(jfif));
    bytes(&wr, jfif, sizeof(jfif));
    for (t = 0; t < 2; t++) {
        marker(&wr, 0xdb, 65);
        *wr.p++ = t;
        for (i = 0; i < 64; i++) *wr.p++ = quant[t][zigzag[i]];
    }
    sof[0] = 8;
    sof[1] = h >> 8; sof[2] = h; sof[3] = w >> 8; sof[4] = w;
    sof[5] = 3;
    sof[6] = 1; sof[7] = 0x22; sof[8] = 0;      /* Y: two by two to a block of colour */
    sof[9] = 2; sof[10] = 0x11; sof[11] = 1;
    sof[12] = 3; sof[13] = 0x11; sof[14] = 1;
    marker(&wr, 0xc0, sizeof(sof));
    bytes(&wr, sof, sizeof(sof));
    for (t = 0; t < 2; t++) {
        marker(&wr, 0xc4, 1 + 16 + 12);
        *wr.p++ = t;
        bytes(&wr, dcbits[t], 16);
        bytes(&wr, dcvals, 12);
        marker(&wr, 0xc4, 1 + 16 + 162);
        *wr.p++ = 0x10 | t;
        bytes(&wr, acbits[t], 16);
        bytes(&wr, acvals[t], 162);
    }
    marker(&wr, 0xda, sizeof(scan));
    bytes(&wr, scan, sizeof(scan));

    for (my = 0; my < h; my += 16)
        for (mx = 0; mx < w; mx += 16) {
            float yb[4][64], cb[64], cr[64];
            memset(cb, 0, sizeof(cb));
            memset(cr, 0, sizeof(cr));
            for (y = 0; y < 16; y++) {
                const uint32_t *row = rgb + (size_t)(my + y < h ? my + y : h - 1) * w;     /* past the edge: the edge */
                for (x = 0; x < 16; x++) {
                    uint32_t p = row[mx + x < w ? mx + x : w - 1];
                    float r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
                    yb[(y / 8) * 2 + x / 8][(y % 8) * 8 + x % 8] = 0.299f * r + 0.587f * g + 0.114f * b - 128.0f;
                    cb[(y / 2) * 8 + x / 2] += (-0.168736f * r - 0.331264f * g + 0.5f * b) * 0.25f;
                    cr[(y / 2) * 8 + x / 2] += (0.5f * r - 0.418688f * g - 0.081312f * b) * 0.25f;
                }
            }
            for (i = 0; i < 4; i++) dcy = block(&wr, yb[i], scale[0], dcy, &hdc[0], &hac[0]);
            dcb = block(&wr, cb, scale[1], dcb, &hdc[1], &hac[1]);
            dcr = block(&wr, cr, scale[1], dcr, &hdc[1], &hac[1]);
        }
    if (wr.nacc) put(&wr, (1 << (8 - wr.nacc)) - 1, 8 - wr.nacc);       /* the last byte is filled with ones */
    *wr.p++ = 0xff;
    *wr.p++ = 0xd9;
    return wr.p - out;
}
