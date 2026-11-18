/* Host check: fxtest <toaster root> <project> <out-prefix> - lists the project's croutons (type, icon, frames),
 * writes <out>-icons.png (all icons) and <out>-<n>-frames.png (8 mask frames of each ANIM effect). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include "effect.h"

static uint8_t *readall(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    long l;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); l = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(l);
    if (b && fread(b, 1, l, f) != (size_t)l) { free(b); b = NULL; }
    fclose(f);
    *n = l;
    return b;
}

static void writepng(const char *path, const uint32_t *px, int w, int h)
{
    FILE *f = fopen(path, "wb");
    png_structp p;
    png_infop info;
    uint8_t *row;
    int x, y;
    if (!f) { perror(path); return; }
    p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info = png_create_info_struct(p);
    row = malloc(w * 3);
    png_init_io(p, f);
    png_set_IHDR(p, info, w, h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(p, info);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) { uint32_t c = px[y * w + x]; row[x * 3] = c >> 16; row[x * 3 + 1] = c >> 8; row[x * 3 + 2] = c; }
        png_write_row(p, row);
    }
    png_write_end(p, NULL);
    png_destroy_write_struct(&p, &info);
    fclose(f);
    free(row);
}

int main(int argc, char **argv)
{
    static char names[1024][256];
    static const char *tn[] = { "other", "ANIM", "ALGO", "ILBM", "MAIN", "CTRL" };
    static const uint32_t mpal[4] = { 0xff00ff, 0x303030, 0x4060c0, 0xe0c040 };   /* 0 ?, 1 matte, 2 A, 3 B */
    size_t n;
    uint8_t *pj;
    int k, i, cols = 8;
    uint32_t *sheet;
    char path[600], out[600];

    if (argc < 4) { fprintf(stderr, "usage: fxtest root project out\n"); return 20; }
    pj = readall(argv[2], &n);
    if (!pj) { perror(argv[2]); return 20; }
    k = project_entries(pj, n, names, NULL, 1024);
    printf("%d entries\n", k);
    sheet = calloc((size_t)cols * 84 * ((k + cols - 1) / cols) * 54, 4);
    for (i = 0; i < k; i++) {
        struct effect e;
        size_t fl;
        uint8_t *fb;
        const char *rest = strchr(names[i], ':');
        int x, y, f;
        snprintf(path, sizeof(path), "%.250s/%.300s", argv[1], rest ? rest + 1 : names[i]);
        for (x = strlen(argv[1]); path[x]; x++)         /* the host copy of the CD is lower case */
            if (path[x] >= 'A' && path[x] <= 'Z') path[x] += 32;
        if (!(fb = readall(path, &fl))) { printf("%2d %-60s missing\n", i, names[i]); continue; }
        effect_open(&e, strrchr(names[i], '/') ? strrchr(names[i], '/') + 1 : names[i], fb, fl);
        {
            char ipath[620];
            size_t il;
            uint8_t *ib;
            snprintf(ipath, sizeof(ipath), "%.600s.i", path);
            if ((ib = readall(ipath, &il))) { effect_merge_icon(&e, ib, il); free(ib); }
        }
        effect_note_path(&e, names[i]);
        if (e.has_icon)
            for (y = 0; y < ICON_H; y++)
                for (x = 0; x < ICON_W; x++)
                    sheet[((size_t)(i / cols) * 54 + 2 + y) * cols * 84 + (i % cols) * 84 + 2 + x] = e.icon[y * ICON_W + x];
        if ((e.type == FX_ANIM || e.type == FX_ILBM) && !effect_frames(&e) && !getenv("NOPNG") && 0) {
        }
        if (e.type == FX_ALGO) effect_frames(&e);
        if (getenv("NOPNG")) {
        } else if (e.type == FX_ALGO && e.algo) {   /* 8 steps with synthetic A (blue ramp) and B (yellow grid) */
            enum { AW = 192, AH = 120 };
            static uint32_t A[AW * AH], Bp[AW * AH], O[AW * AH], strip[AW * 4 * AH * 2];
            int f;
            for (y = 0; y < AH; y++)
                for (x = 0; x < AW; x++) {
                    A[y * AW + x] = 0x2040c0 | (uint32_t)(x * 63 / AW) << 16;
                    Bp[y * AW + x] = ((x / 16 + y / 16) & 1) ? 0xe0c040 : 0x806020;
                }
            for (f = 0; f < 8; f++) {
                algofx_render(e.algo, f, 7, A, Bp, O, AW, AH, 0);
                for (y = 0; y < AH; y++)
                    for (x = 0; x < AW; x++) strip[((size_t)(f / 4) * AH + y) * AW * 4 + (f % 4) * AW + x] = O[y * AW + x];
            }
            snprintf(out, sizeof(out), "%.500s-%02d-algo.png", argv[3], i);
            writepng(out, strip, AW * 4, AH * 2);
        } else if (e.overlay && e.masks) {     /* 8 frames keyed over a blue ramp */
            int fw = FRAME_W / 2, fh = FRAME_H / 2;
            uint32_t *strip = calloc((size_t)fw * 4 * fh * 2, 4);
            for (f = 0; f < 8; f++) {
                int fr = f * (e.nframes - 1) / 7;
                for (y = 0; y < fh; y++)
                    for (x = 0; x < fw; x++) {
                        uint32_t c, v = 0x2040c0 | (uint32_t)(x * 63 / fw) << 16;
                        int a = effect_overlay_px(&e, fr, x * 2, y * 2, e.maincolor == 4 ? 0x10b410 : 0, &c), ch;
                        for (ch = 0; ch < 24; ch += 8)
                            v = (v & ~(255u << ch)) | ((((v >> ch) & 255) * (255 - a) + ((c >> ch) & 255) * a) / 255) << ch;
                        strip[((size_t)(f / 4) * fh + y) * fw * 4 + (f % 4) * fw + x] = v;
                    }
            }
            snprintf(out, sizeof(out), "%.500s-%02d-overlay.png", argv[3], i);
            writepng(out, strip, fw * 4, fh * 2);
            free(strip);
        } else if ((e.type == FX_ANIM || e.type == FX_ILBM) && e.masks) {
            int fw = e.wipemap ? FRAME_W / 2 : e.w / 2, fh = e.wipemap ? FRAME_H / 2 : e.h / 2;
            uint32_t *strip = calloc((size_t)fw * 4 * fh * 2, 4);
            for (f = 0; f < 8; f++) {
                int fr = getenv("CONSEC") ? f + atoi(getenv("CONSEC")) : f * (e.nframes - 1) / 7;
                if (fr >= e.nframes) fr = e.nframes - 1;
                for (y = 0; y < fh; y++)
                    for (x = 0; x < fw; x++)
                    {
                        uint32_t *o = &strip[((size_t)(f / 4) * fh + y) * fw * 4 + (f % 4) * fw + x];
                        if (e.wipemap) {
                            int l = 0, rr = effect_picture(&e, x * FRAME_W / fw, y * FRAME_H / fh, f, 7, &l, o);
                            if (e.wipemap == PIC_MAP) *o = l > 127 ? mpal[3] : mpal[2];
                            else if (rr != FXR_COLOR) *o = rr == FXR_B ? mpal[3] : mpal[2];
                        } else {
                            int v = effect_mask(&e, fr, x * 2, y * 2);
                            *o = e.soft ? e.level[v] * 0x010101u : e.role[v] == FXR_A ? mpal[2] : e.role[v] == FXR_B ? mpal[3] : e.color[v];
                        }
                    }
            }
            snprintf(out, sizeof(out), "%.500s-%02d-frames.png", argv[3], i);
            writepng(out, strip, fw * 4, fh * 2);
            free(strip);
        }
        printf("%s", e.type == FX_ALGO ? (e.algo ? "[algo ok] " : "[algo --] ") : "");
        printf("%s", e.overlay ? (e.masks ? "[ovl ok] " : "[ovl --] ") : "");
        printf("%2d %-5s icon=%d frames=%-3d %dx%d bpp %d soft %d S/M/F %d/%d/%d roles %d%d%d%d %s\n", i, tn[e.type],
               e.has_icon, e.nframes, e.w, e.h, e.bpp, e.soft, e.fcount[0], e.fcount[1], e.fcount[2], e.role[0], e.role[1],
               e.role[2], e.role[3], e.name);
        effect_close(&e);
        free(fb);
    }
    snprintf(out, sizeof(out), "%.500s-icons.png", argv[3]);
    writepng(out, sheet, cols * 84, ((k + cols - 1) / cols) * 54);
    free(sheet);
    free(pj);
    return 0;
}
