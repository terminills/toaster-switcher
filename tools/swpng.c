/* Host check: render the switcher display from a Switcher.main to a PNG.  swpng <Switcher.main.AA> <out.png> [main prvw] */
#include <stdio.h>
#include <stdlib.h>
#include <png.h>
#include <string.h>
#include "switcher.h"

int main(int argc, char **argv)
{
    struct switcher sw;
    FILE *f;
    long n;
    uint8_t *buf;
    png_structp p;
    png_infop info;
    int y, x;
    png_bytep row;

    if (argc < 3) { fprintf(stderr, "usage: swpng exe out.png [main prvw [tbarpos [speed]]]\n"); return 20; }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 20; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    buf = malloc(n);
    if (fread(buf, 1, n, f) != (size_t)n) return 20;
    fclose(f);
    if (sw_open(&sw, buf, n)) { fprintf(stderr, "not a switcher executable\n"); return 20; }
    if (argc > 4) { sw.row[ROW_MAIN].hilite = atoi(argv[3]); sw.row[ROW_PRVW].hilite = atoi(argv[4]); }
    if (argc > 5) sw.tbarpos = atoi(argv[5]);
    strcpy(sw.inlabel[0], "USB3.0 Video");
    strcpy(sw.inlabel[1], "UGREEN Camera 2K");
    strcpy(sw.inlabel[2], "Test Pattern (synthetic)");
    strcpy(sw.inlabel[6], "Black");
    if (argc > 6) sw.speed = atoi(argv[6]);
    printf("T-bar: handle %d rows, travel %d\n", sw.tbarhandle, sw.tbartravel);
    for (y = 0; y < NROWS; y++) printf("row %d: %d FastGadgets\n", y, sw.row[y].n);
    if (getenv("TOASTER_ROOT")) {      /* Editor bar resources */
        char path[600];
        FILE *rf;
        long rn;
        uint8_t *rb;
        snprintf(path, sizeof(path), "%.500s/programs/edit_support/gadget64.pic", getenv("TOASTER_ROOT"));
        if ((rf = fopen(path, "rb"))) {
            fseek(rf, 0, SEEK_END); rn = ftell(rf); fseek(rf, 0, SEEK_SET);
            rb = malloc(rn);
            if (fread(rb, 1, rn, rf) == (size_t)rn) sw.bar.have_sheet = !ilbm_decode(rb, rn, &sw.bar.sheet);
            fclose(rf);
        }
        snprintf(path, sizeof(path), "%.500s/programs/system_support/fonts/interfacelg/11.8c", getenv("TOASTER_ROOT"));
        if ((rf = fopen(path, "rb"))) {
            fseek(rf, 0, SEEK_END); rn = ftell(rf); fseek(rf, 0, SEEK_SET);
            rb = malloc(rn);
            if (fread(rb, 1, rn, rf) == (size_t)rn) sw.bar.have_font = !colorfont_load(&sw.bar.font, rb, rn);
            fclose(rf);
        }
        printf("bar: sheet %d font %d\n", sw.bar.have_sheet, sw.bar.have_font);
    }
    if (getenv("TOASTER_ROOT") && getenv("PROJECT")) {     /* the project's croutons in the grid */
        static char names[64][256];
        static struct effect fx[64];
        long pn;
        uint8_t *pj;
        FILE *pf = fopen(getenv("PROJECT"), "rb");
        if (pf) {
            fseek(pf, 0, SEEK_END); pn = ftell(pf); fseek(pf, 0, SEEK_SET);
            pj = malloc(pn);
            if (fread(pj, 1, pn, pf) == (size_t)pn) {
                int k = project_entries(pj, pn, names, NULL, 64), i;
                for (i = 0; i < k; i++) {
                    char path[600];
                    const char *rest = strchr(names[i], ':');
                    FILE *ef;
                    snprintf(path, sizeof(path), "%.250s/%.300s", getenv("TOASTER_ROOT"), rest ? rest + 1 : names[i]);
                    for (x = strlen(getenv("TOASTER_ROOT")); path[x]; x++)
                        if (path[x] >= 'A' && path[x] <= 'Z') path[x] += 32;
                    if ((ef = fopen(path, "rb"))) {
                        long en;
                        uint8_t *eb;
                        fseek(ef, 0, SEEK_END); en = ftell(ef); fseek(ef, 0, SEEK_SET);
                        eb = malloc(en);
                        if (fread(eb, 1, en, ef) == (size_t)en) effect_open(&fx[sw.nfx++], names[i], eb, en);
                        fclose(ef);
                        strcat(path, ".i");
                        if ((ef = fopen(path, "rb"))) {
                            fseek(ef, 0, SEEK_END); en = ftell(ef); fseek(ef, 0, SEEK_SET);
                            eb = malloc(en);
                            if (fread(eb, 1, en, ef) == (size_t)en) effect_merge_icon(&fx[sw.nfx - 1], eb, en);
                            free(eb);
                            fclose(ef);
                        }
                    }
                }
            }
            fclose(pf);
            sw.fx = fx;
            if (getenv("FXSEL")) sw.fxsel = atoi(getenv("FXSEL"));
            printf("%d croutons\n", sw.nfx);
        }
    }
    sw_render(&sw);
    f = fopen(argv[2], "wb");
    p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info = png_create_info_struct(p);
    png_init_io(p, f);
    png_set_IHDR(p, info, DISPLAYWIDTH, DISPLAYHEIGHT, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(p, info);
    row = malloc(DISPLAYWIDTH * 3);
    for (y = 0; y < DISPLAYHEIGHT; y++) {
        for (x = 0; x < DISPLAYWIDTH; x++) {
            uint32_t c = sw.rgb[y * DISPLAYWIDTH + x];
            row[x * 3] = c >> 16; row[x * 3 + 1] = c >> 8; row[x * 3 + 2] = c;
        }
        png_write_row(p, row);
    }
    png_write_end(p, NULL);
    fclose(f);
    sw_close(&sw);
    return 0;
}
