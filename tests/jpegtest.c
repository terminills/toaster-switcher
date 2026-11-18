/* jpegtest DIR: test pictures through the encoder into DIR, each as NNN.jpg with the picture as it was in NNN.ppm
 * (tests/jpegcheck.py compares what a decoder makes of them). The pictures: the colour bars plug-in's at each place of
 * its stripe, in the size the network stream sends; then sizes that are and are not whole blocks, from flat to noise,
 * so the stream ends in every way it can. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../plugins/jpegenc.h"

static int emit(const char *dir, int n, const uint32_t *rgb, int w, int h)
{
    uint8_t *out = malloc(jpeg_bound(w, h));
    char name[512];
    size_t len;
    FILE *f;
    int i;
    if (!out) return 1;
    len = jpeg_encode(rgb, w, h, out);
    if (len > jpeg_bound(w, h)) {
        fprintf(stderr, "%03d: %dx%d took %zu bytes, %zu allowed\n", n, w, h, len, jpeg_bound(w, h));
        return 1;
    }
    snprintf(name, sizeof(name), "%s/%03d.jpg", dir, n);
    if (!(f = fopen(name, "wb"))) return 1;
    fwrite(out, 1, len, f);
    fclose(f);
    snprintf(name, sizeof(name), "%s/%03d.ppm", dir, n);
    if (!(f = fopen(name, "wb"))) return 1;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (i = 0; i < w * h; i++) {
        uint8_t px[3] = { (uint8_t)(rgb[i] >> 16), (uint8_t)(rgb[i] >> 8), (uint8_t)rgb[i] };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    free(out);
    return 0;
}

int main(int argc, char **argv)
{
    static const uint32_t bar[7] = { 0xbfbfbf, 0xbfbf00, 0x00bfbf, 0x00bf00, 0xbf00bf, 0xbf0000, 0x0000bf };
    enum { W = 480, H = 360, PLACES = 60, RANDOM = 240 };
    uint32_t *rgb = malloc(W * H * 4);
    int n, x, y;
    if (argc != 2 || !rgb) return 2;
    for (n = 0; n < PLACES; n++) {              /* as plugins/bars.c draws it */
        int split = H * 2 / 3, stripe = n * W / PLACES, sw = W / 40 + 1;
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++)
                rgb[y * W + x] = y < split ? bar[x * 7 / W] : x >= stripe && x < stripe + sw ? 0xffffff : 0;
        if (emit(argv[1], n, rgb, W, H)) return 1;
    }
    srand(1);
    for (; n < PLACES + RANDOM; n++) {
        int w = 1 + rand() % 70, h = 1 + rand() % 50, amp = 1 << (rand() % 9), i;
        uint32_t base = (uint32_t)rand() & 0xffffff;
        for (i = 0; i < w * h; i++)
            rgb[i] = (((base >> 16) + rand() % amp) & 255) << 16 | (((base >> 8) + rand() % amp) & 255) << 8 |
                     ((base + rand() % amp) & 255);
        if (emit(argv[1], n, rgb, w, h)) return 1;
    }
    printf("%d pictures\n", n);
    free(rgb);
    return 0;
}
