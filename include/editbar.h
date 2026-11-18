/* The Editor's button bar under the crouton grid (Delete, All, Clone, Tools, Controls, transport, Save, Programs,
 * Views), drawn from NewTek's Edit_Support/Gadget64.pic and the InterfaceLG font. */
#ifndef EDITBAR_H_INCLUDED
#define EDITBAR_H_INCLUDED
#include <stdint.h>
#include "gfxfile.h"

#define EDITBAR_Y 247
#define EDITBAR_H 20

enum { EB_DELETE, EB_ALL, EB_CLONE, EB_TOOLS, EB_CONTROLS, EB_PLAY, EB_PREVIEWPLAY, EB_STOP, EB_SAVE, EB_PROGRAMS,
       EB_VIEWS, EB_COUNT };

struct editbar {
    struct picture sheet;       /* Gadget64.pic */
    struct colorfont font;      /* InterfaceLG */
    int have_sheet, have_font;
    int x[EB_COUNT], w[EB_COUNT];
    uint32_t lit;               /* buttons framed in the highlight colour, one bit each (play while playing) */
};

/* Lay out the bar; sheet/font may be missing (then buttons are drawn plain). */
void editbar_init(struct editbar *eb);
void editbar_draw(const struct editbar *eb, uint32_t *rgb, int stride, int height);
/* Button under x,y or -1. */
int editbar_hit(const struct editbar *eb, int x, int y);
#endif
