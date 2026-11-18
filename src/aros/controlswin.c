/* The Controls window (see controlswin.h): modal, the switcher waits while it is open. */
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <stdio.h>
#include <string.h>
#include "controlswin.h"

enum { GID_SPEED = 1, GID_HOLD, GID_OK, GID_CANCEL };

int controls_edit(struct Window *parent, const char *name, int *speed, int *hold, int dflt)
{
    static const char *const speeds[] = { "Panel (S M F)", "Slow", "Medium", "Fast", NULL };
    struct Screen *scr = parent->WScreen;
    struct Gadget *glist = NULL, *g, *gspeed, *ghold;
    struct NewGadget ng;
    struct Window *win = NULL;
    APTR vi;
    UWORD fh = scr->Font->ta_YSize, lw, ww, wh, left, top, bw;
    int sp = *speed + 1, done = 0, ok = 0;
    char title[96];

    if (!(vi = GetVisualInfoA(scr, NULL))) return 0;
    lw = TextLength(&scr->RastPort, (CONST_STRPTR)"Hold (seconds)", 14) + 8;
    bw = 8 * fh;
    left = scr->WBorLeft + 8;
    top = scr->WBorTop + fh + 1 + 8;
    ww = left + lw + 16 * fh + 8 + scr->WBorRight;
    wh = top + 3 * (fh + 10) + 8 + scr->WBorBottom;

    g = CreateContext(&glist);
    memset(&ng, 0, sizeof(ng));
    ng.ng_VisualInfo = vi;
    ng.ng_TextAttr = scr->Font;
    ng.ng_LeftEdge = left + lw; ng.ng_TopEdge = top; ng.ng_Width = 16 * fh; ng.ng_Height = fh + 6;
    ng.ng_GadgetText = (UBYTE *)"Speed"; ng.ng_GadgetID = GID_SPEED; ng.ng_Flags = PLACETEXT_LEFT;
    g = gspeed = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (IPTR)speeds, GTCY_Active, sp, TAG_DONE);
    ng.ng_TopEdge += fh + 10; ng.ng_Width = 6 * fh;
    ng.ng_GadgetText = (UBYTE *)"Hold (seconds)"; ng.ng_GadgetID = GID_HOLD;
    g = ghold = CreateGadget(INTEGER_KIND, g, &ng, GTIN_Number, (*hold >= 0 ? *hold : dflt) / 50, GTIN_MaxChars, 4,
                             TAG_DONE);
    ng.ng_TopEdge += fh + 10; ng.ng_LeftEdge = left; ng.ng_Width = bw;
    ng.ng_GadgetText = (UBYTE *)"OK"; ng.ng_GadgetID = GID_OK; ng.ng_Flags = PLACETEXT_IN;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_LeftEdge = ww - scr->WBorRight - 8 - bw;
    ng.ng_GadgetText = (UBYTE *)"Cancel"; ng.ng_GadgetID = GID_CANCEL;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);

    snprintf(title, sizeof(title), "Controls: %.80s", name);
    if (g)
        win = OpenWindowTags(NULL, WA_Title, (IPTR)title, WA_Width, ww, WA_Height, wh,
                             WA_Left, parent->LeftEdge + (parent->Width - ww) / 2, WA_Top, parent->TopEdge + parent->Height / 3,
                             WA_PubScreen, (IPTR)scr, WA_Gadgets, (IPTR)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                             WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
                             WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_RAWKEY | BUTTONIDCMP |
                                 CYCLEIDCMP | INTEGERIDCMP, TAG_DONE);
    if (win) {
        GT_RefreshWindow(win, NULL);
        while (!done) {
            struct IntuiMessage *m;
            WaitPort(win->UserPort);
            while ((m = GT_GetIMsg(win->UserPort))) {
                ULONG cl = m->Class;
                UWORD code = m->Code;
                struct Gadget *gad = (struct Gadget *)m->IAddress;
                GT_ReplyIMsg(m);
                if (cl == IDCMP_REFRESHWINDOW) {
                    GT_BeginRefresh(win);
                    GT_EndRefresh(win, TRUE);
                } else if (cl == IDCMP_CLOSEWINDOW || (cl == IDCMP_RAWKEY && code == 0x45)) {
                    done = 1;
                } else if (cl == IDCMP_GADGETUP) {
                    if (gad->GadgetID == GID_SPEED) sp = code;
                    else if (gad->GadgetID == GID_OK) done = ok = 1;
                    else if (gad->GadgetID == GID_CANCEL) done = 1;
                }
            }
        }
        if (ok) {
            IPTR n = 0;
            int h;
            GT_GetGadgetAttrs(ghold, win, NULL, GTIN_Number, (IPTR)&n, TAG_DONE);
            h = (int)n < 0 ? 0 : (int)n > 3600 ? 3600 * 50 : (int)n * 50;
            ok = sp - 1 != *speed || (h != dflt ? h : -1) != *hold;
            *speed = sp - 1;
            *hold = *hold < 0 && h == dflt ? -1 : h;
        }
        CloseWindow(win);
    }
    FreeGadgets(glist);
    FreeVisualInfo(vi);
    return ok;
}
