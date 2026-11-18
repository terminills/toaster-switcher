/* CG pages as croutons (effect.c, cgpage.c): pagetest makes pages of its own and checks what is read from them
 * and where they are, field by field; pagetest <drawer> <page file>... says what each file is and writes fields of
 * it over a ramp, as the Switcher shows them (480x360), into the drawer as PPM; pagetest match finds a page's field
 * in pictures of Program (see match). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "effect.h"
#include "croutontags.h"

static int bad;
#define CHECK(c, ...) do { if (!(c)) { printf("FAILED %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); bad++; } } while (0)

struct out { uint8_t *d; size_t n, cap; };
static void put(struct out *o, const void *p, size_t n)
{
    if (o->n + n > o->cap) o->d = realloc(o->d, o->cap = (o->n + n) * 2);
    memcpy(o->d + o->n, p, n);
    o->n += n;
}
static void put32(struct out *o, uint32_t v) { uint8_t b[4] = { v >> 24, v >> 16, v >> 8, v }; put(o, b, 4); }
static void put16(struct out *o, unsigned v) { uint8_t b[2] = { v >> 8, v }; put(o, b, 2); }
static size_t begin(struct out *o, const char *id) { put(o, id, 4); put32(o, 0); return o->n; }
static void end(struct out *o, size_t at)
{
    uint32_t l = (uint32_t)(o->n - at);
    uint8_t b[4] = { l >> 24, l >> 16, l >> 8, l };
    memcpy(o->d + at - 4, b, 4);
    if (l & 1) put(o, "", 1);
}

/* FORM ILBM of rgba in 24 planes and a mask plane, or 32 planes; packed as literal runs when comp. */
static void ilbm(struct out *o, const uint8_t *rgba, int w, int h, int planes, int top, int comp)
{
    size_t form = begin(o, "FORM"), c;
    int rb = (w + 15) / 16 * 2, y, pl, x, rows = planes == 24 ? 25 : 32;
    uint8_t *row = malloc(rb);
    put(o, "ILBM", 4);
    c = begin(o, "BMHD");
    put16(o, w); put16(o, h); put16(o, 0); put16(o, top);
    put(o, (uint8_t[]){ planes, planes == 24, comp, 0 }, 4);
    put16(o, 0); put(o, (uint8_t[]){ 10, 11 }, 2); put16(o, w); put16(o, h);
    end(o, c);
    c = begin(o, "BODY");
    for (y = 0; y < h; y++)
        for (pl = 0; pl < rows; pl++) {
            memset(row, 0, rb);
            for (x = 0; x < w; x++) {
                const uint8_t *p = rgba + ((size_t)y * w + x) * 4;
                if (pl < planes ? p[pl / 8] >> (pl & 7) & 1 : p[3] >= 128) row[x >> 3] |= 0x80 >> (x & 7);
            }
            for (x = 0; x < rb; x += 128) {
                int n = rb - x < 128 ? rb - x : 128;
                if (comp) put(o, (uint8_t[]){ n - 1 }, 1);
                put(o, row + x, n);
            }
        }
    end(o, c);
    end(o, form);
    free(row);
}

/* A page file: the picture, an icon, and the CrUD with tags (id, value pairs) and the page after them. */
static uint8_t *page(const char *type, const uint8_t *rgba, int w, int h, int planes, int top, int comp,
                     const uint32_t *tags, int ntags, size_t *n)
{
    struct out o = { 0 };
    uint8_t icon[ICON_W * ICON_H * 4];
    size_t form, c;
    int i;
    for (i = 0; i < ICON_W * ICON_H; i++) {
        icon[i * 4] = (uint8_t)(i % ICON_W * 3); icon[i * 4 + 1] = (uint8_t)(i / ICON_W * 5); icon[i * 4 + 2] = 77;
        icon[i * 4 + 3] = 255;
    }
    ilbm(&o, rgba, w, h, planes, top, comp);
    ilbm(&o, icon, ICON_W, ICON_H, 24, 0, 1);
    form = begin(&o, "FORM");
    put(&o, "CrUD", 4);
    c = begin(&o, "TYPE"); put(&o, type, 4); put32(&o, 0); end(&o, c);
    c = begin(&o, "LIBS"); put(&o, "effects.library", 16); end(&o, c);
    c = begin(&o, "TAGS");
    for (i = 0; i < ntags * 2; i++) put32(&o, tags[i]);
    put32(&o, !strcmp(type, "FRAM") ? 0x10000069 : 0x69);      /* the page: a size and what looks like tags */
    put32(&o, 12);
    put32(&o, 0x80000011); put32(&o, 1); put32(&o, 0x8000006a);
    put32(&o, 0); put32(&o, 0);
    end(&o, c);
    end(&o, form);
    *n = o.n;
    return o.d;
}

static uint8_t *picture(int w, int h, int alpha)
{
    uint8_t *p = malloc((size_t)w * h * 4);
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint8_t *d = p + ((size_t)y * w + x) * 4;
            d[0] = (uint8_t)(x * 7 + y); d[1] = (uint8_t)(y * 3); d[2] = (uint8_t)(x ^ y);
            d[3] = !alpha ? 255 : (x / 16 + y / 8) % 3 == 0 ? 0 : (x / 16 + y / 8) % 3 == 1 ? 255 : (uint8_t)(x + y * 5);
        }
    return p;
}

static uint32_t ramp(int x, int y) { return (uint32_t)(x * 255 / CGPAGE_W) << 16 | (uint32_t)(y * 255 / CGPAGE_H) << 8 | 0x40; }

/* The page at pos (pixels of the frame) over the ramp, at full size: every pixel against the picture. */
static void field(struct effect *e, const uint8_t *src, long fields, long pos, const char *what)
{
    static uint32_t buf[CGPAGE_W * CGPAGE_H];
    struct cgpage *pg = e->page;
    long pos2;
    int x, y, wrong = 0;
    for (y = 0; y < CGPAGE_H; y++)
        for (x = 0; x < CGPAGE_W; x++) buf[y * CGPAGE_W + x] = ramp(x, y);
    CHECK(cgpage_at(pg, fields, &pos2), "%s: over at field %ld", what, fields);
    CHECK(pg->kind == CGP_STILL || (pos2 >= 0 ? pos2 / 2 : -((1 - pos2) / 2)) == pos, "%s: field %ld at %ld half pixels, not at pixel %ld", what, fields, pos2, pos);
    cgpage_blend(pg, pos2, buf, CGPAGE_W, CGPAGE_H, 4, 4);
    for (y = 0; y < CGPAGE_H; y++)
        for (x = 0; x < CGPAGE_W; x++) {
            long sx = pg->kind == CGP_CRAWL ? x + pos : x, sy = pg->kind == CGP_SCROLL ? y + pos : y - pg->top;
            uint32_t want = ramp(x, y);
            if (sx >= 0 && sx < pg->w && sy >= 0 && sy < pg->h) {
                const uint8_t *s = src + ((size_t)sy * pg->w + sx) * 4;
                int a = s[3], c;
                uint32_t o = 0;
                for (c = 0; c < 3; c++) o = o << 8 | (((want >> (16 - 8 * c)) & 255) * (255 - a) / 255 + s[c] * a / 255);
                want = o;
            }
            if (buf[y * CGPAGE_W + x] != want && !wrong++)
                printf("FAILED %s: field %ld, pixel %d,%d is %06x, not %06x\n", what, fields, x, y, (unsigned)buf[y * CGPAGE_W + x], (unsigned)want);
        }
    bad += wrong != 0;
}

/* One page made and read back; its fields counted against what a player stepping it would take. */
static void one(const char *type, int w, int h, int planes, int top, int comp, int speed, int hold, int loop)
{
    uint8_t *src = picture(w, h, planes == 32), *file;
    uint32_t tags[8] = { TAG_VALUE(TAGN_Duration), 0, 0x8000006a, (uint32_t)speed, 0x80000087, (uint32_t)hold,
                         0x80f70003, (uint32_t)loop };
    struct effect e;
    size_t n, i;
    char what[80];
    int scroll = !strcmp(type, "SCRO"), moves = scroll || !strcmp(type, "CRAW");
    long sp2 = moves ? (scroll ? (speed ? 2 * speed : 1) : 4 << speed) : 0, frame = scroll ? CGPAGE_H : CGPAGE_W;
    long len = scroll ? h : w, steps = 0, k, pos2;

    snprintf(what, sizeof(what), "%s %dx%d speed %d%s%s", type, w, h, speed, hold ? " freeze" : "", loop ? " loop" : "");
    if (moves) {                /* a player moves sp2 half pixels a field from -frame until the strip has left */
        long at = 0;
        while (at < (frame + len) * 2) { at += sp2; steps++; }
        tags[1] = (uint32_t)steps;
    }
    file = page(type, src, w, h, planes, top, comp, tags, moves ? 3 + loop : 0, &n);
    CHECK(!effect_open(&e, what, file, n), "%s: not opened", what);
    CHECK(e.has_icon && e.icon[0] == 0x00004d && e.icon[ICON_W + 2] == 0x06054d, "%s: icon %d, %06x %06x", what, e.has_icon, (unsigned)e.icon[0], (unsigned)e.icon[ICON_W + 2]);
    CHECK(e.overlay == (strcmp(type, "FRAM") != 0), "%s: overlay %d", what, e.overlay);
    CHECK(e.tagslen == (moves ? 8u * (3 + loop) : 0u), "%s: %u bytes of tags", what, (unsigned)e.tagslen);
    CHECK(!effect_frames(&e) && e.page, "%s: no page", what);
    if (e.page) {
        struct cgpage *pg = e.page;
        CHECK(pg->w == w && pg->h == h && pg->top == top, "%s: read as %dx%d at %d", what, pg->w, pg->h, pg->top);
        for (i = 0; i < (size_t)w * h; i++) {
            const uint8_t *a = src + i * 4, *b = pg->rgba + i * 4;
            int alpha = planes == 32 ? a[3] : a[3] >= 128 ? 255 : 0;
            if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2] || alpha != b[3]) break;
        }
        CHECK(i == (size_t)w * h, "%s: pixel %lu differs", what, (unsigned long)i);
        if (planes == 24) for (i = 0; i < (size_t)w * h; i++) src[i * 4 + 3] = src[i * 4 + 3] >= 128 ? 255 : 0;
        CHECK(pg->speed2 == sp2, "%s: %d half pixels a field, not %ld", what, pg->speed2, sp2);
        if (!moves) {
            field(&e, src, 0, 0, what);
            CHECK(cgpage_at(pg, 100000, &pos2) && !pos2, "%s: a still page is over", what);
        } else if (loop) {
            field(&e, src, 0, -frame, what);
            field(&e, src, steps + 7, -frame + 7 * sp2 / 2, what);
            CHECK(cgpage_at(pg, steps * 1000 - 1, &pos2), "%s: a loop is over", what);
        } else if (hold && scroll) {
            long stop = (frame + len - CGPAGE_SAFE_BOTTOM) * 2;
            field(&e, src, (stop + sp2 - 1) / sp2, h - CGPAGE_SAFE_BOTTOM, what);
            field(&e, src, 1000000, h - CGPAGE_SAFE_BOTTOM, what);
            field(&e, src, 3, -frame + 3 * sp2 / 2, what);
        } else {
            for (k = 0; cgpage_at(pg, k, &pos2); k++)
                ;
            CHECK(k == steps, "%s: over after %ld fields, not %ld", what, k, steps);
            CHECK(!cgpage_at(pg, 2000000000, &pos2), "%s: back on air", what);
            field(&e, src, 0, -frame, what);
            field(&e, src, steps / 2, -frame + steps / 2 * sp2 / 2, what);
            field(&e, src, steps - 1, -frame + (steps - 1) * sp2 / 2, what);
        }
        {                       /* scaled as the Switcher shows it: nothing but the page's own values, in its place */
            static uint32_t small[480 * 360];
            int x, y, left = 480, right = -1, up = 360, down = -1;
            memset(small, 0, sizeof(small));
            cgpage_at(pg, moves ? (hold && scroll ? 1000000 : steps / 2) : 0, &pos2);
            cgpage_blend(pg, pos2, small, 480, 360, 256, 256);
            for (y = 0; y < 360; y++)
                for (x = 0; x < 480; x++)
                    if (small[y * 480 + x]) {
                        if (x < left) left = x;
                        if (x > right) right = x;
                        if (y < up) up = y;
                        if (y > down) down = y;
                    }
            printf("%-34s %s, 480x360 covers %d,%d to %d,%d\n", what, moves ? "plays" : "still", left, up, right, down);
            CHECK(right >= left, "%s: nothing drawn at 480x360", what);
        }
    }
    for (i = 0; i < n; i += n / 37 + 1) {       /* cut short anywhere: refused or read, never read past its end */
        struct effect t;
        uint8_t *cut = malloc(i + 1);
        memcpy(cut, file, i);
        effect_open(&t, "cut", cut, i);
        t.type = e.type;
        effect_frames(&t);
        effect_close(&t);
        free(cut);
    }
    effect_close(&e);
    free(file);
    free(src);
}

/* A picture as NewTek's CG wrote it: the framestore's planes named by PLTP. A grey bar of the level lv in columns
 * x0 to x1 of every line, keyed (18 planes) with its key six columns behind it and a half-level column at each
 * end, or a frame (16 planes). What is read must be that bar where it is, opaque, and nothing beside it. */
static void framestore_file(int keyed, int top, int h)
{
    static const uint8_t pltp[36] = { 6,7, 6,6, 6,5, 6,4, 6,3, 6,2, 6,1, 6,0, 7,7, 7,6, 7,5, 7,4, 7,3, 7,2, 7,1, 7,0, 11,1, 11,0 };
    const int w = 768, x0 = 200, x1 = 520, lv = 160, planes = keyed ? 18 : 16, rb = w / 8;
    struct out o = { 0 };
    struct cgpage pg = { 0 };
    size_t form = begin(&o, "FORM"), c;
    uint8_t row[768 / 8];
    int x, y, pl, wrong = 0;
    put(&o, "ILBM", 4);
    c = begin(&o, "BMHD");
    put16(&o, w); put16(&o, h); put16(&o, 0); put16(&o, top);
    put(&o, (uint8_t[]){ planes, 0, 1, 0 }, 4);
    put16(&o, 0); put(&o, (uint8_t[]){ 10, 11 }, 2); put16(&o, w); put16(&o, 0);
    end(&o, c);
    c = begin(&o, "PLTP"); put(&o, pltp, planes * 2); end(&o, c);
    c = begin(&o, "BODY");
    for (y = 0; y < h; y++)
        for (pl = 0; pl < planes; pl++) {
            memset(row, 0, sizeof(row));
            for (x = 0; x < w; x++) {
                int in = x >= x0 && x < x1, kx = x - 6;
                int v = pltp[pl * 2] == 11 ? (kx == x0 || kx == x1 - 1 ? 2 : kx > x0 && kx < x1 - 1 ? 3 : 0)
                                           : (in ? lv : 70);       /* both frames alike: no chroma */
                if (v >> pltp[pl * 2 + 1] & 1) row[x >> 3] |= 0x80 >> (x & 7);
            }
            for (x = 0; x < rb; x += 128) {
                int n = rb - x < 128 ? rb - x : 128;
                put(&o, (uint8_t[]){ n - 1 }, 1);
                put(&o, row + x, n);
            }
        }
    end(&o, c);
    end(&o, form);
    CHECK(!cgpage_decode(&pg, o.d, o.n) && pg.w == CGPAGE_W && pg.h == CGPAGE_H, "framestore file, %d planes: not read", planes);
    if (pg.rgba) {
        int grey = (int)((lv - 70.5) / 129.5 * 255 + 0.5);
        for (y = 0; y < CGPAGE_H; y++)
            for (x = 0; x < CGPAGE_W; x++) {
                const uint8_t *d = pg.rgba + ((size_t)y * CGPAGE_W + x) * 4;
                int lines = y >= top && y < top + h, in = lines && x >= x0 && x < x1;
                int a = !keyed ? 255 : !in ? 0 : x == x0 || x == x1 - 1 ? 170 : 255;
                /* the bar's ends ring in the chroma: its colour is checked two columns in */
                if (d[3] != a || (in && x >= x0 + 2 && x < x1 - 2 && (abs(d[0] - grey) > 1 || abs(d[1] - grey) > 1 || abs(d[2] - grey) > 1)))
                    if (!wrong++) printf("framestore file, %d planes: %d,%d is %d %d %d alpha %d, not %d alpha %d\n", planes, x, y,
                                         d[0], d[1], d[2], d[3], in ? grey : 0, a);
            }
        CHECK(!wrong, "framestore file, %d planes: %d pixels wrong", planes, wrong);
    }
    cgpage_free(&pg);
    free(o.d);
}

static uint8_t *readall(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b = NULL;
    long l;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); l = ftell(f); fseek(f, 0, SEEK_SET);
    if (l > 0 && (b = malloc(l)) && fread(b, 1, l, f) != (size_t)l) { free(b); b = NULL; }
    fclose(f);
    *n = (size_t)l;
    return b;
}

static void show(const char *dir, const char *path)
{
    static const char *const tn[] = { "KEYF", "FRAM", "SCRO", "CRAW" };
    static const char *const mode[] = { "once", "freeze", "loop" };
    static uint32_t buf[480 * 360];
    const char *name = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    struct effect e;
    size_t n;
    uint8_t *file = readall(path, &n);
    uint32_t dur = 0;
    long k = 0, pos2, fields[4];
    int i, x, y;

    if (!file) { printf("%s: not read\n", path); bad++; return; }
    effect_open(&e, name, file, n);
    if (e.type < FX_KEYF || effect_frames(&e)) { printf("%s: not a CG page\n", path); bad++; free(file); return; }
    if (effect_moves(&e) && e.page->mode == CGP_ONCE)
        while (cgpage_at(e.page, k, &pos2)) k++;
    effect_tag(&e, TAG_VALUE(TAGN_Duration), &dur, NULL);
    printf("%s: %s %dx%d at line %d, icon %d, overlay %d, %d half pixels a field, %s, Duration %lu, over after %ld fields\n", name,
           tn[e.type - FX_KEYF], e.page->w, e.page->h, e.page->top, e.has_icon, e.overlay, e.page->speed2,
           mode[e.page->mode], (unsigned long)dur, k);
    if (effect_moves(&e) && e.page->mode == CGP_ONCE) CHECK(k == (long)dur, "%s: Duration %lu, over after %ld fields", name, (unsigned long)dur, k);
    fields[0] = 0; fields[1] = dur / 4; fields[2] = dur / 2; fields[3] = dur ? dur - 1 : 0;
    for (i = 0; i < (effect_moves(&e) ? 4 : 1); i++) {
        char out[600];
        FILE *f;
        for (y = 0; y < 360; y++)
            for (x = 0; x < 480; x++) buf[y * 480 + x] = (uint32_t)(40 + x / 4) << 16 | (uint32_t)(60 + y / 4) << 8 | 120;
        if (cgpage_at(e.page, fields[i], &pos2)) cgpage_blend(e.page, pos2, buf, 480, 360, 256, 256);
        snprintf(out, sizeof(out), "%.400s/%.100s-%ld.ppm", dir, name, fields[i]);
        if ((f = fopen(out, "wb"))) {
            fprintf(f, "P6 480 360 255\n");
            for (x = 0; x < 480 * 360; x++) fwrite((uint8_t[]){ buf[x] >> 16, buf[x] >> 8, buf[x] }, 1, 3, f);
            fclose(f);
        }
    }
    if (e.has_icon) {
        char out[600];
        FILE *f;
        snprintf(out, sizeof(out), "%.400s/%.100s-icon.ppm", dir, name);
        if ((f = fopen(out, "wb"))) {
            fprintf(f, "P6 %d %d 255\n", ICON_W, ICON_H);
            for (x = 0; x < ICON_W * ICON_H; x++) fwrite((uint8_t[]){ e.icon[x] >> 16, e.icon[x] >> 8, e.icon[x] }, 1, 3, f);
            fclose(f);
        }
    }
    effect_close(&e);
    free(file);
}

static uint32_t *readppm(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint32_t *px = NULL;
    uint8_t rgb[3];
    int w = 0, h = 0, max = 0, i;
    if (!f) return NULL;
    if (fscanf(f, "P6 %d %d %d", &w, &h, &max) == 3 && w == 480 && h == 360 && max == 255 && fgetc(f) != EOF &&
        (px = malloc(480 * 360 * sizeof(*px))))
        for (i = 0; i < 480 * 360; i++) {
            if (fread(rgb, 1, 3, f) != 3) { free(px); px = NULL; break; }
            px[i] = (uint32_t)rgb[0] << 16 | (uint32_t)rgb[1] << 8 | rgb[2];
        }
    fclose(f);
    return px;
}

static long apart(const uint32_t *a, const uint32_t *b)
{
    long d = 0;
    int x, y, c;
    for (y = 0; y < 360; y += 2)
        for (x = 0; x < 480; x += 2)
            if ((b[y * 480 + x] & 0xffffff) != 0xff00ff)
                for (c = 0; c < 24; c += 8) d += labs((long)(a[y * 480 + x] >> c & 255) - (long)(b[y * 480 + x] >> c & 255));
    return d;
}

/* pagetest match <page file> <background.ppm or -> <first> <last> <picture.ppm>...: which field of the page over the
 * background (480x360 pictures, - is black) each picture is nearest to, of the fields first to last; for a page that
 * stands still, at which level of 256. Printed with how far apart they are (the sum over every fourth pixel), and
 * how far the picture is from the background alone. What is FF00FF in a picture is not Program (a monitor's level
 * meter, painted over) and is left out. "track" for "match" takes the pictures as one recording of a
 * moving page: once a picture shows the page, the next ones are looked for from 4 fields before it to 40 after. */
static int match(int argc, char **argv)
{
    static uint32_t buf[480 * 360];
    struct effect e;
    size_t n;
    uint8_t *file = readall(argv[2], &n);
    uint32_t *bg = strcmp(argv[3], "-") ? readppm(argv[3]) : calloc(480 * 360, sizeof(*bg));
    long first = atol(argv[4]), last = atol(argv[5]), k, pos2, prev = -1;
    int i, track = !strcmp(argv[1], "track");

    if (!file || !bg) { printf("%s or %s: not read\n", argv[2], argv[3]); return 10; }
    effect_open(&e, "page", file, n);
    if (e.type < FX_KEYF || effect_frames(&e)) { printf("%s: not a CG page\n", argv[2]); return 10; }
    for (i = 6; i < argc; i++) {
        uint32_t *pic = readppm(argv[i]);
        long best = -1, at = -1, d, from = first, to = last, off;
        if (!pic) { printf("%s: not read\n", argv[i]); bad++; continue; }
        off = apart(bg, pic);
        if (track && prev >= 0 && effect_moves(&e)) {
            if (from < prev - 4) from = prev - 4;
            if (to > prev + 40) to = prev + 40;
        }
        for (k = from; k <= to; k++) {
            memcpy(buf, bg, sizeof(buf));
            if (effect_moves(&e)) {
                if (cgpage_at(e.page, k, &pos2)) cgpage_blend(e.page, pos2, buf, 480, 360, 256, 256);
            } else {
                cgpage_blend(e.page, 0, buf, 480, 360, (int)k, 256);
            }
            d = apart(buf, pic);
            if (best < 0 || d < best) { best = d; at = k; }
        }
        if (best < off) prev = at;
        printf("%s: %s %ld, apart %ld; from the background %ld\n", argv[i], effect_moves(&e) ? "field" : "level", at, best, off);
        free(pic);
    }
    effect_close(&e);
    free(file);
    free(bg);
    return bad ? 10 : 0;
}

int main(int argc, char **argv)
{
    int i;
    if (argc > 6 && (!strcmp(argv[1], "match") || !strcmp(argv[1], "track"))) return match(argc, argv);
    if (argc > 2) {
        for (i = 2; i < argc; i++) show(argv[1], argv[i]);
        return bad ? 10 : 0;
    }
    one("KEYF", CGPAGE_W, CGPAGE_H, 32, 0, 1, 0, 0, 0);
    one("FRAM", CGPAGE_W, CGPAGE_H, 24, 0, 1, 0, 0, 0);
    one("FRAM", CGPAGE_W, CGPAGE_H, 24, 0, 0, 0, 0, 0);
    for (i = 0; i <= 8; i++) one("SCRO", CGPAGE_W, 700 + i, 32, 0, 1, i, 0, 0);
    one("SCRO", CGPAGE_W, 901, 32, 0, 1, 0, 1, 0);
    one("SCRO", CGPAGE_W, 900, 32, 0, 0, 3, 1, 0);
    one("SCRO", CGPAGE_W, 333, 32, 0, 1, 2, 0, 1);
    for (i = 0; i <= 3; i++) one("CRAW", 3001 + i, 48, 32, 380, 1, i, 0, 0);
    one("CRAW", 500, 61, 32, 20, 1, 1, 0, 1);
    framestore_file(1, 43, 400);
    framestore_file(0, 0, 480);
    printf(bad ? "%d checks FAILED\n" : "CG pages: all checks passed\n", bad);
    return bad ? 10 : 0;
}
