/* The interface/switcher display: DrawFastGList (fastgadgets.a) render modes, written to an RGB buffer
 * instead of the 2-3 interface bitplanes the copper lists showed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "switcher.h"

/* Interface colours as shown on the A4000 reference (rig/shots/ref-project3.png, mapped through the highlighted
 * buttons); planes 0-2 select the entry. Measured from the interiors of areas of one value, not yet taken from the
 * program's copper lists; 6 and 7 were not found in uniform areas. 0 is black: the lettering of the headers and
 * the logo (the panel grey around them is 1 under a clear mask). */
static const uint32_t pal_norm[8] = {
    0x000000, 0x525050, 0x919391, 0xdbdbdb, 0x2c2c2c, 0x454645, 0x383838, 0xb2b3b2
};
/* Highlighted FastGadgets have their mask word in plane 2. */
static const uint32_t pal_hilite[8] = {
    0x525050, 0x000000, 0x919391, 0xc8c8c8, 0x706b52, 0x2c2c2c, 0xbeae6f, 0xd5c46f
};

static void find_tbar(struct switcher *sw);

/* ROW_BGCOLOR: entry 0 is the list head; 1..9 BLACK WHITE RED GREEN BLUE YELLOW PURPLE CYAN SNOW. Their FG_IndexIDs
 * are the card's matte values; these are broadcast-safe stand-ins. */
static const struct { const char *name; long rgb; } bgcol[10] = {
    { "", 0 }, { "Black", 0x101010 }, { "White", 0xebebeb }, { "Red", 0xb41010 }, { "Green", 0x10b410 },
    { "Blue", 0x1010b4 }, { "Yellow", 0xb4b410 }, { "Purple", 0xb410b4 }, { "Cyan", 0x10b4b4 }, { "Snow", -1 } };

long sw_bgcolor(int i) { return i >= 1 && i <= 9 ? bgcol[i].rgb : 0x101010; }
const char *sw_bgname(int i) { return i >= 1 && i <= 9 ? bgcol[i].name : ""; }

int sw_open(struct switcher *sw, const uint8_t *exe, size_t n)
{
    static const uint16_t field[NROWS] = { TB_SliceFGL, TB_LumKeyFGL, TB_MainFGL, TB_PrvwFGL, TB_GridSelFGL, TB_TransFGL,
                                           TB_TBarFGL, TB_FMCountFGL, TB_NumPadFGL, TB_ClipFGL, TB_MiscFGL, TB_BGColorFGL };
    int r;

    memset(sw, 0, sizeof(*sw));
    if (hunkimg_load(&sw->img, exe, n) < 0) return -1;
    /* SwitcherCode starts with  lea DataPool,a5  (41f9/4bf9 + absolute address) */
    if (hi_w(&sw->img, hi_hunk(&sw->img, 0)) != 0x4bf9) return -2;
    sw->datapool = hi_l(&sw->img, hi_hunk(&sw->img, 0) + 2);
    for (r = 0; r < NROWS; r++) {
        uint32_t fg = hi_l(&sw->img, sw->datapool + field[r]);
        struct fgrow *row = &sw->row[r];
        row->hilite = -1;
        while (fg && row->n < MAXFG && hi_valid(&sw->img, fg, FG_BorderCon + 2)) {
            row->fg[row->n++] = fg;
            fg = hi_l(&sw->img, fg + gg_NextGadget);
        }
    }
    find_tbar(sw);
    editbar_init(&sw->bar);
    sw->bgcolor = 1;
    sw->keyclip = 128;
    sw->row[ROW_CLIP].hilite = CLIP_OFF;
    sw->speed = 1;
    sw->fxsel = -1;
    sw->rgb = calloc(DISPLAYWIDTH * DISPLAYHEIGHT, sizeof(uint32_t));
    return sw->rgb ? 0 : -3;
}

void sw_close(struct switcher *sw)
{
    free(sw->rgb);
    hunkimg_free(&sw->img);
}

/* 16 pixels; where mask has a 0 bit the screen is left as it is. */
static void put16(uint32_t *rgb, uint32_t off, uint16_t p0, uint16_t p1, uint16_t p2, uint16_t mask, const uint32_t *pal)
{
    int b;
    uint32_t y = off / BYTESPERROW, x = (off % BYTESPERROW) * 8;
    if (y >= DISPLAYHEIGHT || x + 16 > DISPLAYWIDTH) return;
    for (b = 0; b < 16; b++) {
        int s = 15 - b;
        if ((mask >> s) & 1)
            rgb[y * DISPLAYWIDTH + x + b] = pal[((p0 >> s) & 1) | ((p1 >> s) & 1) << 1 | ((p2 >> s) & 1) << 2];
    }
}

/* Rows [r0, r1) of one FastGadget, drawn dy rows lower; modes 2 and 4 of NormTab/HiLiteTab (AACHIPS).
 * hilite: highlight words hx0 <= word < hx1 of each row. opaque: draw under a clear mask too (the T-bar stem
 * painted over where the handle was). */
static void draw_rows(struct switcher *sw, uint32_t fg, int r0, int r1, int dy, int hilite, int hx0, int hx1,
                      int opaque)
{
    const struct hunkimg *h = &sw->img;
    uint16_t mode = hi_w(h, fg + FG_ModeType), ww = hi_w(h, fg + FG_WWidth);
    uint16_t mod = hi_w(h, fg + FG_Modulus);
    uint32_t off = hi_l(h, fg + FG_Offset), src = hi_l(h, fg + FG_Data);
    uint32_t words = (uint16_t)(ww + 1), bytes = mode == 8 ? 4 : 8;
    int y;
    uint32_t x;

    if (!src || (mode != 8 && mode != 16)) return;
    src += (uint32_t)r0 * words * bytes;
    off += (uint32_t)(r0 + dy) * BYTESPERROW;
    for (y = r0; y < r1; y++) {
        uint32_t o = off;
        for (x = 0; x < words; x++) {
            int hl = hilite && (int)x >= hx0 && (int)x < hx1;
            if (mode == 8) {                /* mode 2: two planes */
                put16(sw->rgb, o, hi_w(h, src), hi_w(h, src + 2), 0, 0xffff, pal_norm);
            } else {                        /* mode 4: mask word + three planes; highlighted, the mask is plane 2 */
                uint16_t m = hi_w(h, src);
                put16(sw->rgb, o, hi_w(h, src + 2), hi_w(h, src + 4), hl ? m : hi_w(h, src + 6), hl || opaque ? 0xffff : m,
                      hl ? pal_hilite : pal_norm);
            }
            src += bytes;
            o += 2;
        }
        off += BYTESPERROW;
    }
    (void)mod;
}

static void draw_fg(struct switcher *sw, uint32_t fg, int hilite)
{
    const struct hunkimg *h = &sw->img;
    if (hi_b(h, fg + FG_DispFlag)) return;
    /* the picture can be a word wider than the button: only the button's own words highlight */
    draw_rows(sw, fg, 0, hi_w(h, fg + FG_Height) + 1, 0, hilite, 0, (hi_w(h, fg + gg_Width) + 15) / 16, 0);
}

/* The T-bar: the stem stays, the handle rows slide down it by tbarpos. */
static void draw_tbar(struct switcher *sw, uint32_t fg)
{
    const struct hunkimg *h = &sw->img;
    int rows = hi_w(h, fg + FG_Height) + 1, y;
    if (hi_b(h, fg + FG_DispFlag)) return;
    draw_rows(sw, fg, 0, rows, 0, 0, 0, 0, 0);                      /* T-bar at rest */
    if (!sw->tbarpos) return;
    for (y = 0; y < sw->tbarpos && y < rows; y++)                   /* stem where the handle was */
        draw_rows(sw, fg, rows - 1, rows, y - (rows - 1), 0, 0, 0, 1);
    draw_rows(sw, fg, 0, sw->tbarhandle, sw->tbarpos, 0, 0, 0, 0);  /* handle, lower */
}

/* The T-bar picture's handle ends where its rows start repeating the stem's last row. */
static void find_tbar(struct switcher *sw)
{
    const struct hunkimg *h = &sw->img;
    uint32_t fg, src, words, bytes, last;
    int rows, r;
    if (sw->row[ROW_TBAR].n <= TBAR_FG) return;
    fg = sw->row[ROW_TBAR].fg[TBAR_FG];
    src = hi_l(h, fg + FG_Data);
    rows = hi_w(h, fg + FG_Height) + 1;
    words = (uint16_t)(hi_w(h, fg + FG_WWidth) + 1);
    bytes = hi_w(h, fg + FG_ModeType) == 8 ? 4 : 8;
    if (!src || rows < 8) return;
    last = src + (uint32_t)(rows - 1) * words * bytes;
    for (r = rows - 2; r > 0; r--) {
        uint32_t row = src + (uint32_t)r * words * bytes, k;
        for (k = 0; k < words * bytes && hi_b(h, row + k) == hi_b(h, last + k); k++)
            ;
        if (k < words * bytes) break;
    }
    sw->tbarhandle = r + 1;
    sw->tbartravel = rows - sw->tbarhandle;
}

/* The Editor grid: the project's croutons, the selected one framed in the highlight colour. */
static void draw_grid(struct switcher *sw)
{
    int i, x, y;
    for (i = sw->fxtop * GRID_COLS; i < sw->nfx && i < (sw->fxtop + GRID_ROWS) * GRID_COLS; i++) {
        int cx = GRID_X + (i % GRID_COLS) * GRID_PITCHX, cy = GRID_Y + (i / GRID_COLS - sw->fxtop) * GRID_PITCHY;
        const struct effect *e = &sw->fx[i];
        if (!e->has_icon) continue;
        for (y = 0; y < ICON_H; y++)
            memcpy(sw->rgb + (size_t)(cy + y) * DISPLAYWIDTH + cx, e->icon + y * ICON_W, ICON_W * 4);
        if (i == sw->fxsel)
            for (y = -2; y < ICON_H + 2; y++)
                for (x = -2; x < ICON_W + 2; x++)
                    if ((x < 0 || x >= ICON_W || y < 0 || y >= ICON_H) && cy + y >= 0)
                        sw->rgb[(size_t)(cy + y) * DISPLAYWIDTH + cx + x] = pal_hilite[6];
    }
}

int sw_hit_crouton(const struct switcher *sw, int x, int y)
{
    int c, r, i;
    if (x < GRID_X || y < GRID_Y) return -1;
    c = (x - GRID_X) / GRID_PITCHX;
    r = (y - GRID_Y) / GRID_PITCHY;
    if (c >= GRID_COLS || r >= GRID_ROWS || (x - GRID_X) % GRID_PITCHX >= ICON_W || (y - GRID_Y) % GRID_PITCHY >= ICON_H)
        return -1;
    i = (sw->fxtop + r) * GRID_COLS + c;
    return i < sw->nfx ? i : -1;
}

void sw_render(struct switcher *sw)
{
    int r, i;
    for (i = 0; i < DISPLAYWIDTH * DISPLAYHEIGHT; i++)
        sw->rgb[i] = pal_norm[1];       /* the panel */
    draw_grid(sw);
    editbar_draw(&sw->bar, sw->rgb, DISPLAYWIDTH, DISPLAYHEIGHT);
    for (r = 0; r < NROWS; r++)
        if (r != ROW_BGCOLOR || sw->bgpopup)
            for (i = 0; i < sw->row[r].n; i++) {
                uint32_t fg = sw->row[r].fg[i];
                if (r == ROW_TBAR && i == TBAR_FG)
                    draw_tbar(sw, fg);
                else if (r == ROW_PRVW && i == PRVW_DIVIDER) {
                    if (!hi_b(&sw->img, fg + FG_DispFlag))
                        draw_rows(sw, fg, 0, hi_w(&sw->img, fg + FG_Height) + 1, LABEL_H, 0, 0, 0, 0);
                }
                else if (r == ROW_FMCOUNT)                          /* S M F */
                    draw_fg(sw, fg, i == sw->speed);
                else if (r == ROW_TRANS)
                    draw_fg(sw, fg, i < 32 && (sw->translit >> i & 1));
                else
                    draw_fg(sw, fg, r == ROW_BGCOLOR ? i == sw->bgcolor : i == sw->row[r].hilite);
            }
    if (sw->bar.have_font) {        /* each input's stream under its Preview button, cut to the button's width */
        static const uint32_t lpal[8] = { 0xdbdbdb, CF_TRANSPARENT, CF_TRANSPARENT, CF_TRANSPARENT, CF_TRANSPARENT,
                                          CF_TRANSPARENT, CF_TRANSPARENT, CF_TRANSPARENT };
        for (i = 0; i < sw->row[ROW_PRVW].n && i < PRVW_DIVIDER && i < 8; i++) {
            char t[32];
            int x, y, w, h, n, tw = 0;
            if (!sw->inlabel[i][0]) continue;
            sw_box(sw, ROW_PRVW, i, &x, &y, &w, &h);
            strncpy(t, sw->inlabel[i], sizeof(t) - 1);
            t[sizeof(t) - 1] = 0;
            for (n = strlen(t); n > 0 && (tw = colorfont_width(&sw->bar.font, t)) > w; t[--n] = 0)
                ;
            if (n) colorfont_draw(&sw->bar.font, sw->rgb, DISPLAYWIDTH, x + w, DISPLAYHEIGHT, x + (w - tw) / 2, y + h,
                                  t, lpal);
        }
    }
    if (sw->bar.have_font && sw->row[ROW_TRANS].n > TRANS_BGCOLOR) {     /* the BKG colour's name in its box */
        static const uint32_t pal[8] = { 0x000000, CF_TRANSPARENT, CF_TRANSPARENT, 0xdbdbdb, 0x2c2c2c, 0x454645,
                                         0x383838, 0xb2b3b2 };
        int x, y, w, h;
        sw_box(sw, ROW_TRANS, TRANS_BGCOLOR, &x, &y, &w, &h);
        colorfont_draw(&sw->bar.font, sw->rgb, DISPLAYWIDTH, x + w - 18, DISPLAYHEIGHT, x + 8,
                       y + (h - sw->bar.font.ysize) / 2 + 1, sw_bgname(sw->bgcolor), pal);
        if (sw->row[ROW_CLIP].n > CLIP_LEVEL) {                     /* the Superimpose clip level */
            char t[8];
            snprintf(t, sizeof(t), "%d", sw->keyclip);
            sw_box(sw, ROW_CLIP, CLIP_LEVEL, &x, &y, &w, &h);
            colorfont_draw(&sw->bar.font, sw->rgb, DISPLAYWIDTH, x + w - 18, DISPLAYHEIGHT, x + 8,
                           y + (h - sw->bar.font.ysize) / 2 + 1, t, pal);
        }
    }
}

void sw_box(const struct switcher *sw, int row, int index, int *x, int *y, int *w, int *h)
{
    uint32_t fg = row >= 0 && row < NROWS && index >= 0 && index < sw->row[row].n ? sw->row[row].fg[index] : 0;
    *x = fg ? (int16_t)hi_w(&sw->img, fg + gg_LeftEdge) : 0;
    *y = fg ? (int16_t)hi_w(&sw->img, fg + gg_TopEdge) : 0;
    *w = fg ? (int16_t)hi_w(&sw->img, fg + gg_Width) : 0;
    *h = fg ? (int16_t)hi_w(&sw->img, fg + gg_Height) : 0;
}

int sw_hit(const struct switcher *sw, int x, int y, int *row, int *index)
{
    const struct hunkimg *h = &sw->img;
    int r, i;
    for (r = 0; r < NROWS; r++)
        for (i = 0; i < sw->row[r].n; i++) {
            uint32_t fg = sw->row[r].fg[i];
            int l = (int16_t)hi_w(h, fg + gg_LeftEdge), t = (int16_t)hi_w(h, fg + gg_TopEdge);
            int w = (int16_t)hi_w(h, fg + gg_Width), hh = (int16_t)hi_w(h, fg + gg_Height);
            if (r == ROW_BGCOLOR && !sw->bgpopup) continue;
            if (!hi_l(h, fg + FG_Function)) continue;      /* headers, frames, dividers: not buttons */
            if (!hi_b(h, fg + FG_DispFlag) && x >= l && x < l + w && y >= t && y < t + hh) {
                *row = r; *index = i;
                return 1;
            }
        }
    return 0;
}
