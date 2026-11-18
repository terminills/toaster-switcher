/* The Toaster's look, drawn (see skin.h). */
#include <stddef.h>
#include "skin.h"
#include "gfxfile.h"

/* Gadget64.pic colours: outline, bevel, face, shade; and the same of a button held down */
#define C_OUT 0x202020
#define C_LIGHT 0xd0d0d0
#define C_FACE SKIN_FACE
#define C_SHADE 0x505050
#define C_DOWNLIGHT 0x909090
#define C_DOWNFACE SKIN_FACE_DOWN
#define C_DOWNSHADE 0x404040

static void put(uint32_t *rgb, int stride, int height, int x, int y, uint32_t c)
{
    if (x >= 0 && x < stride && y >= 0 && y < height) rgb[(size_t)y * stride + x] = c;
}

void skin_fill(uint32_t *rgb, int stride, int height, int x0, int y0, int w, int h, uint32_t colour)
{
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            put(rgb, stride, height, x0 + x, y0 + y, colour);
}

void skin_button(uint32_t *rgb, int stride, int height, int x0, int y0, int w, int h, int flags)
{
    uint32_t light = flags & SKIN_DOWN ? C_DOWNLIGHT : C_LIGHT, face = flags & SKIN_DOWN ? C_DOWNFACE : C_FACE;
    uint32_t shade = flags & SKIN_DOWN ? C_DOWNSHADE : C_SHADE;
    int x, y, ax = x0 + w - 14;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint32_t c = face;
            int corner = (x < 2 || x >= w - 2) && (y < 2 || y >= h - 2);
            if (corner) continue;
            if (y == 0 || y == h - 1 || x == 0 || x == w - 1) c = C_OUT;
            else if (y <= 2 || x <= 2) c = light;
            else if (y >= h - 3 || x >= w - 3) c = shade;
            else if ((flags & SKIN_ARROWS) && x0 + x == ax - 2) c = C_OUT;      /* divider before the arrows */
            put(rgb, stride, height, x0 + x, y0 + y, c);
        }
    if (flags & SKIN_ARROWS)
        for (y = 0; y < 4; y++)
            for (x = -y; x <= y; x++) {
                put(rgb, stride, height, ax + 5 + x, y0 + 5 + y, 0x000000);
                put(rgb, stride, height, ax + 5 + x, y0 + h - 6 - y, 0x000000);
            }
}

void skin_well(uint32_t *rgb, int stride, int height, int x0, int y0, int w, int h)
{
    skin_fill(rgb, stride, height, x0, y0, w, h, 0x2c2c2c);
    skin_fill(rgb, stride, height, x0 + 2, y0 + 2, w - 2, h - 2, 0x919391);
    skin_fill(rgb, stride, height, x0 + 2, y0 + 2, w - 3, h - 3, SKIN_WELL);
}

void skin_rule(uint32_t *rgb, int stride, int height, int x0, int y0, int w)
{
    skin_fill(rgb, stride, height, x0, y0, w, 2, 0x2c2c2c);
    skin_fill(rgb, stride, height, x0, y0 + 2, w, 1, 0x454645);
}

/* part of 256 of colour over ground */
static uint32_t blend(uint32_t colour, uint32_t ground, int part)
{
    uint32_t c = 0;
    int sh;
    for (sh = 0; sh < 24; sh += 8) {
        int f = (colour >> sh) & 0xff, g = (ground >> sh) & 0xff;
        c |= (uint32_t)(g + (f - g) * part / 256) << sh;
    }
    return c;
}

/* The fonts are black letters (pen 0) on a button's face (pen 2), with edges in the pens of the three greys between:
 * how far each of those is from the face to black is how much of the letter it stands for. */
void skin_ink(uint32_t pal[8], uint32_t colour, uint32_t ground)
{
    int i;
    for (i = 0; i < 8; i++) pal[i] = CF_TRANSPARENT;
    pal[0] = colour;
    pal[4] = blend(colour, ground, 179);
    pal[5] = blend(colour, ground, 133);
    pal[1] = blend(colour, ground, 111);
}
