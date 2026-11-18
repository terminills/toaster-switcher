/* The Editor's button bar: sheet crops for the named buttons, NewTek-style drawn popups for Tools/Programs/Views. */
#include <string.h>
#include "editbar.h"
#include "skin.h"

#define C_FACE 0x909090         /* Gadget64.pic's button face */

static const struct { int sx, sy, sw; const char *label; } item[EB_COUNT] = {
    { 260, 0, 56, NULL },       /* Delete */
    { 188, 44, 35, NULL },      /* All: shown pressed (the third, darker row of the sheet), as on the Toaster */
    { 317, 0, 59, NULL },       /* Clone */
    { 0, 0, 62, "Tools" },
    { 115, 0, 68, NULL },       /* Controls */
    { 595, 416, 38, NULL },     /* play */
    { 492, 457, 38, NULL },     /* play on Preview */
    { 242, 165, 35, NULL },     /* stop */
    { 394, 0, 47, NULL },       /* Save */
    { 0, 0, 92, "Programs" },
    { 0, 0, 68, "Views" },
};

void editbar_init(struct editbar *eb)
{
    int i, x = 64;
    for (i = 0; i < EB_COUNT; i++) {
        eb->x[i] = x;
        eb->w[i] = item[i].sw;
        x += item[i].sw + 4;
    }
}

static void put(uint32_t *rgb, int stride, int height, int x, int y, uint32_t c)
{
    if (x >= 0 && x < stride && y >= 0 && y < height) rgb[(size_t)y * stride + x] = c;
}

/* A popup button in Gadget64.pic's style, with its name. */
static void popup(const struct editbar *eb, uint32_t *rgb, int stride, int height, int x0, int w, const char *label)
{
    /* the font's pixel values are interface pens */
    static const uint32_t pal[8] = { 0x000000, 0x525050, C_FACE, 0xdbdbdb, 0x2c2c2c, 0x454645, 0x383838, 0xb2b3b2 };
    int y0 = EDITBAR_Y;
    skin_button(rgb, stride, height, x0, y0, w, EDITBAR_H, SKIN_ARROWS);
    if (eb->have_font) {
        int tw = colorfont_width(&eb->font, label);
        colorfont_draw(&eb->font, rgb, stride, x0 + w - 16, height, x0 + (w - 16 - tw) / 2 + 1,
                       y0 + (EDITBAR_H - eb->font.ysize) / 2 + 2, label, pal);
    }
}

void editbar_draw(const struct editbar *eb, uint32_t *rgb, int stride, int height)
{
    int i, x, y;
    for (i = 0; i < EB_COUNT; i++) {
        if (item[i].label) {
            popup(eb, rgb, stride, height, eb->x[i], eb->w[i], item[i].label);
            continue;
        }
        if (!eb->have_sheet) continue;
        for (y = 0; y < EDITBAR_H; y++)
            for (x = 0; x < item[i].sw; x++) {
                int sx = item[i].sx + x, sy = item[i].sy + y;
                if (sx < eb->sheet.w && sy < eb->sheet.h)
                    put(rgb, stride, height, eb->x[i] + x, EDITBAR_Y + y, eb->sheet.rgb[(size_t)sy * eb->sheet.w + sx]);
            }
        if (eb->lit >> i & 1)
            for (y = 0; y < EDITBAR_H; y++)
                for (x = 0; x < item[i].sw; x++)
                    if (x < 2 || x >= item[i].sw - 2 || y < 2 || y >= EDITBAR_H - 2)
                        put(rgb, stride, height, eb->x[i] + x, EDITBAR_Y + y, 0xbeae6f);
    }
}

int editbar_hit(const struct editbar *eb, int x, int y)
{
    int i;
    if (y < EDITBAR_Y || y >= EDITBAR_Y + EDITBAR_H) return -1;
    for (i = 0; i < EB_COUNT; i++)
        if (x >= eb->x[i] && x < eb->x[i] + eb->w[i]) return i;
    return -1;
}
