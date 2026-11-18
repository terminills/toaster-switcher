/* Drawing in the Toaster's own look, for what NewTek's pictures do not have: buttons in the style of
 * Edit_Support/Gadget64.pic, sunken wells, the rules of the panel's headings. All into a 0x00RRGGBB buffer of
 * stride pixels a row and height rows; what falls outside it is left out. */
#ifndef SKIN_H
#define SKIN_H
#include <stdint.h>

#define SKIN_PANEL 0x525050     /* the panel's grey */
#define SKIN_TEXT 0xdbdbdb      /* lettering on it */
#define SKIN_HILITE 0xbeae6f    /* what is selected */
#define SKIN_FACE 0x909090      /* a button's face */
#define SKIN_FACE_DOWN 0x505050 /* and of one held down */
#define SKIN_GHOST 0x6c6c6c     /* the lettering of a button that cannot be used */
#define SKIN_WELL 0x454645      /* the inside of a well */
#define SKIN_BUTTON_H 20        /* the buttons of Gadget64.pic */

enum { SKIN_DOWN = 1,           /* held down */
       SKIN_ARROWS = 2 };       /* up and down arrows at the right: it offers a choice */

void skin_fill(uint32_t *rgb, int stride, int height, int x, int y, int w, int h, uint32_t colour);
void skin_button(uint32_t *rgb, int stride, int height, int x, int y, int w, int h, int flags);
void skin_well(uint32_t *rgb, int stride, int height, int x, int y, int w, int h);
void skin_rule(uint32_t *rgb, int stride, int height, int x, int y, int w);
/* The palette that letters in NewTek's interface fonts (colorfont_draw) in colour on ground, with the soft edges
 * the fonts have around their letters. */
void skin_ink(uint32_t pal[8], uint32_t colour, uint32_t ground);
#endif
