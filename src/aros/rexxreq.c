/* The Editor's requesters (see rexxreq.h): reqtools where it has one, GadTools windows for the others. */
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <libraries/reqtools.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/reqtools.h>
#include <stdio.h>
#include <string.h>
#include "rexxreq.h"

#define WINTITLE "Video Toaster"
#define MAXBOXES 4
#define MAXNAMES 32
#define MAXLINES 4

enum { GID_BOX = 1, GID_LIST = 50, GID_OK, GID_CANCEL };

int req_tell(struct Window *w, const char *const *lines, int n)
{
    struct Library *ReqToolsBase = OpenLibrary((CONST_STRPTR)"reqtools.library", 38);
    struct TagItem tags[] = { { RT_Window, (IPTR)w }, { RT_ReqPos, REQPOS_CENTERWIN }, { RTEZ_ReqTitle, (IPTR)WINTITLE },
                              { TAG_DONE, 0 } };
    char text[1024];
    size_t o = 0;
    int i, r;
    if (!ReqToolsBase) return 0;
    for (i = 0; i < n; i++) {           /* the body is a format */
        const char *s = lines[i];
        if (i && o + 1 < sizeof(text)) text[o++] = '\n';
        for (; *s && o + 2 < sizeof(text); s++) {
            if (*s == '%') text[o++] = '%';
            text[o++] = *s;
        }
    }
    text[o] = 0;
    r = rtEZRequestA(text, "Continue|Cancel", NULL, NULL, tags) != 0;
    CloseLibrary(ReqToolsBase);
    return r;
}

int req_number(struct Window *w, const char *title, long *v, long min, long max)
{
    struct Library *ReqToolsBase = OpenLibrary((CONST_STRPTR)"reqtools.library", 38);
    struct TagItem tags[] = { { RT_Window, (IPTR)w }, { RT_ReqPos, REQPOS_CENTERWIN },
                              { min <= max ? RTGL_Min : TAG_IGNORE, (IPTR)min },
                              { min <= max ? RTGL_Max : TAG_IGNORE, (IPTR)max }, { TAG_DONE, 0 } };
    ULONG n = (ULONG)*v;
    int r;
    if (!ReqToolsBase) return 0;
    if ((r = rtGetLongA(&n, (char *)title, NULL, tags) != 0)) *v = (LONG)n;
    CloseLibrary(ReqToolsBase);
    return r;
}

int req_string(struct Window *w, const char *title, char *s, size_t size)
{
    struct Library *ReqToolsBase = OpenLibrary((CONST_STRPTR)"reqtools.library", 38);
    struct TagItem tags[] = { { RT_Window, (IPTR)w }, { RT_ReqPos, REQPOS_CENTERWIN }, { RTGS_AllowEmpty, TRUE },
                              { TAG_DONE, 0 } };
    int r;
    if (!ReqToolsBase) return 0;
    r = rtGetStringA((UBYTE *)s, size - 1, title, NULL, tags) != 0;
    CloseLibrary(ReqToolsBase);
    return r;
}

int req_time(struct Window *w, const char *title, char *tc)
{
    char s[16];
    snprintf(s, sizeof(s), "%.11s", tc);
    for (;;) {
        unsigned f[4] = { 0, 0, 0, 0 }, k = 0, digits = 0, bad = 0;
        const char *p;
        if (!req_string(w, title, s, sizeof(s))) return 0;
        for (p = s; *p && !bad; p++) {          /* up to four numbers: the last one is the frames */
            if (*p >= '0' && *p <= '9' && digits < 2) {
                f[k] = f[k] * 10 + (*p - '0');
                digits++;
            } else if (*p == ':' && digits && k < 3) {
                k++;
                digits = 0;
            } else {
                bad = 1;
            }
        }
        if (!bad && digits) {
            unsigned t[4] = { 0, 0, 0, 0 }, i;
            for (i = 0; i <= k; i++) t[3 - k + i] = f[i];
            t[2] += t[3] / 30;                  /* "90" is three seconds */
            t[1] += t[2] / 60;
            t[0] += t[1] / 60;
            if (t[0] < 100) {
                snprintf(tc, 12, "%02u:%02u:%02u:%02u", t[0], t[1] % 60, t[2] % 60, t[3] % 30);
                return 1;
            }
        }
        DisplayBeep(w->WScreen);
    }
}

static struct Window *notice;

void req_notice_close(void)
{
    if (notice) CloseWindow(notice);
    notice = NULL;
}

void req_notice(struct Window *w, const char *const *lines, int n)
{
    struct Screen *scr = w->WScreen;
    struct DrawInfo *dri;
    UWORD fh = scr->Font->ta_YSize, tw = 0;
    int i;
    req_notice_close();
    if (n > MAXLINES) n = MAXLINES;
    for (i = 0; i < n; i++) {
        UWORD l = TextLength(&scr->RastPort, (CONST_STRPTR)lines[i], strlen(lines[i]));
        if (l > tw) tw = l;
    }
    if (tw < 12 * fh) tw = 12 * fh;
    notice = OpenWindowTags(NULL, WA_Title, (IPTR)WINTITLE, WA_InnerWidth, tw + 32, WA_InnerHeight, n * (fh + 2) + 24,
                            WA_Left, w->LeftEdge + (w->Width - tw - 32) / 2, WA_Top, w->TopEdge + w->Height / 3,
                            WA_PubScreen, (IPTR)scr, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_SmartRefresh, TRUE,
                            WA_NoCareRefresh, TRUE, TAG_DONE);
    if (!notice) return;
    SetFont(notice->RPort, scr->RastPort.Font);
    if ((dri = GetScreenDrawInfo(scr))) {
        SetAPen(notice->RPort, dri->dri_Pens[TEXTPEN]);
        FreeScreenDrawInfo(scr, dri);
    }
    SetDrMd(notice->RPort, JAM1);
    for (i = 0; i < n; i++) {
        Move(notice->RPort, notice->BorderLeft + 16, notice->BorderTop + 12 + i * (fh + 2) + notice->RPort->TxBaseline);
        Text(notice->RPort, (CONST_STRPTR)lines[i], strlen(lines[i]));
    }
}

void req_message(struct Window *w, const char *text)
{
    static char title[128];             /* the window keeps the pointer */
    snprintf(title, sizeof(title), "%s", text ? text : "");
    SetWindowTitles(w, (CONST_STRPTR)(*title ? title : WINTITLE), (CONST_STRPTR)-1);
}

int req_file(struct Window *w, const char *title, const char *dir, const char *file, char *out, size_t size)
{
    struct Library *ReqToolsBase = OpenLibrary((CONST_STRPTR)"reqtools.library", 38);
    struct rtFileRequester *fr;
    char name[108];
    int r = 0;
    if (!ReqToolsBase) return 0;
    snprintf(name, sizeof(name), "%s", file);
    if ((fr = rtAllocRequestA(RT_FILEREQ, NULL))) {
        if (*dir) rtChangeReqAttr(fr, RTFI_Dir, (IPTR)dir, TAG_DONE);
        if (rtFileRequest(fr, name, (char *)title, RT_Window, (IPTR)w, RT_ReqPos, REQPOS_CENTERWIN, TAG_DONE)) {
            snprintf(out, size, "%s", (const char *)fr->Dir);
            AddPart((STRPTR)out, (CONST_STRPTR)name, size);
            r = 1;
        }
        rtFreeRequest(fr);
    }
    CloseLibrary(ReqToolsBase);
    return r;
}

/* A window of gadgets over parent, wide enough for its title; ww and wh without the borders' share. */
static struct Window *dialog(struct Window *parent, const char *title, UWORD ww, UWORD wh, struct Gadget *glist,
                             ULONG idcmp)
{
    struct Screen *scr = parent->WScreen;
    UWORD tw = TextLength(&scr->RastPort, (CONST_STRPTR)title, strlen(title)) + 6 * scr->Font->ta_YSize + 16;
    if (ww < tw) ww = tw;
    return OpenWindowTags(NULL, WA_Title, (IPTR)title, WA_Width, ww, WA_Height, wh,
                          WA_Left, parent->LeftEdge + (parent->Width - ww) / 2, WA_Top, parent->TopEdge + parent->Height / 3,
                          WA_PubScreen, (IPTR)scr, WA_Gadgets, (IPTR)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                          WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
                          WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_RAWKEY | BUTTONIDCMP | idcmp, TAG_DONE);
}

/* The two buttons at the bottom of a dialog ww wide. */
static struct Gadget *buttons(struct Gadget *g, struct NewGadget *ng, struct Screen *scr, const char *ok, UWORD ww,
                              UWORD top)
{
    UWORD bw = 8 * scr->Font->ta_YSize;
    ng->ng_TopEdge = top; ng->ng_LeftEdge = scr->WBorLeft + 8; ng->ng_Width = bw;
    ng->ng_Height = scr->Font->ta_YSize + 6;
    ng->ng_GadgetText = (UBYTE *)ok; ng->ng_GadgetID = GID_OK; ng->ng_Flags = PLACETEXT_IN;
    g = CreateGadget(BUTTON_KIND, g, ng, TAG_DONE);
    ng->ng_LeftEdge = ww - scr->WBorRight - 8 - bw;
    ng->ng_GadgetText = (UBYTE *)"Cancel"; ng->ng_GadgetID = GID_CANCEL;
    return CreateGadget(BUTTON_KIND, g, ng, TAG_DONE);
}

int req_buttons(struct Window *parent, const char *title, char *states, const char *const *labels, int n)
{
    struct Screen *scr = parent->WScreen;
    struct Gadget *glist = NULL, *g, *box[MAXBOXES];
    struct NewGadget ng;
    struct Window *win = NULL;
    APTR vi;
    UWORD fh = scr->Font->ta_YSize, lw = 0, ww, wh, left, top, minw;
    int i, done = 0, ok = 0;

    if (n > MAXBOXES) n = MAXBOXES;
    if (!(vi = GetVisualInfoA(scr, NULL))) return 0;
    for (i = 0; i < n; i++) {
        UWORD l = TextLength(&scr->RastPort, (CONST_STRPTR)labels[i], strlen(labels[i]));
        if (l > lw) lw = l;
    }
    left = scr->WBorLeft + 8;
    top = scr->WBorTop + fh + 1 + 8;
    ww = left + 2 * fh + 8 + lw + 8 + scr->WBorRight;
    minw = TextLength(&scr->RastPort, (CONST_STRPTR)title, strlen(title)) + 6 * fh + 16;
    if (minw < left + 16 * fh + 24 + scr->WBorRight) minw = left + 16 * fh + 24 + scr->WBorRight;
    if (ww < minw) ww = minw;
    wh = top + (n + 1) * (fh + 10) + 8 + scr->WBorBottom;

    g = CreateContext(&glist);
    memset(&ng, 0, sizeof(ng));
    ng.ng_VisualInfo = vi;
    ng.ng_TextAttr = scr->Font;
    ng.ng_LeftEdge = left; ng.ng_TopEdge = top; ng.ng_Width = 2 * fh; ng.ng_Height = fh + 6;
    ng.ng_Flags = PLACETEXT_RIGHT;
    for (i = 0; i < n; i++) {
        ng.ng_GadgetText = (UBYTE *)labels[i]; ng.ng_GadgetID = GID_BOX + i;
        g = box[i] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, states[i] == '1', GTCB_Scaled, TRUE, TAG_DONE);
        ng.ng_TopEdge += fh + 10;
    }
    g = buttons(g, &ng, scr, "Continue", ww, ng.ng_TopEdge);
    if (g) win = dialog(parent, title, ww, wh, glist, CHECKBOXIDCMP);
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
                } else if (cl == IDCMP_RAWKEY && (code == 0x44 || code == 0x43)) {      /* Return, Enter */
                    done = ok = 1;
                } else if (cl == IDCMP_GADGETUP) {
                    if (gad->GadgetID == GID_OK) done = ok = 1;
                    else if (gad->GadgetID == GID_CANCEL) done = 1;
                }
            }
        }
        for (i = 0; ok && i < n; i++) {
            IPTR on = 0;
            GT_GetGadgetAttrs(box[i], win, NULL, GTCB_Checked, (IPTR)&on, TAG_DONE);
            states[i] = on ? '1' : '0';
        }
        CloseWindow(win);
    }
    FreeGadgets(glist);
    FreeVisualInfo(vi);
    return ok;
}

int req_choose(struct Window *parent, const char *title, const char *const *names, int n)
{
    struct Screen *scr = parent->WScreen;
    struct Gadget *glist = NULL, *g;
    struct NewGadget ng;
    struct Window *win = NULL;
    struct List list;
    struct Node node[MAXNAMES];
    APTR vi;
    UWORD fh = scr->Font->ta_YSize, lw = 0, ww, wh, left, top, rows;
    ULONG secs = 0, micros = 0;
    int i, done = 0, sel = 0, ok = 0;

    if (n > MAXNAMES) n = MAXNAMES;
    if (n < 1 || !(vi = GetVisualInfoA(scr, NULL))) return -1;
    NewList(&list);
    for (i = 0; i < n; i++) {
        UWORD l = TextLength(&scr->RastPort, (CONST_STRPTR)names[i], strlen(names[i]));
        if (l > lw) lw = l;
        memset(&node[i], 0, sizeof(node[i]));
        node[i].ln_Name = (char *)names[i];
        AddTail(&list, &node[i]);
    }
    rows = n < 4 ? 4 : n > 12 ? 12 : n;
    if (lw < 16 * fh) lw = 16 * fh;
    left = scr->WBorLeft + 8;
    top = scr->WBorTop + fh + 1 + 8;
    ww = left + lw + 40 + 8 + scr->WBorRight;
    wh = top + rows * (fh + 2) + 8 + 8 + fh + 6 + 8 + scr->WBorBottom;

    g = CreateContext(&glist);
    memset(&ng, 0, sizeof(ng));
    ng.ng_VisualInfo = vi;
    ng.ng_TextAttr = scr->Font;
    ng.ng_LeftEdge = left; ng.ng_TopEdge = top; ng.ng_Width = lw + 40; ng.ng_Height = rows * (fh + 2) + 8;
    ng.ng_GadgetID = GID_LIST;
    g = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (IPTR)&list, GTLV_ShowSelected, (IPTR)NULL,
                     GTLV_Selected, 0, TAG_DONE);
    g = buttons(g, &ng, scr, "OK", ww, top + rows * (fh + 2) + 8 + 8);
    if (g) win = dialog(parent, title, ww, wh, glist, LISTVIEWIDCMP);
    if (win) {
        GT_RefreshWindow(win, NULL);
        while (!done) {
            struct IntuiMessage *m;
            WaitPort(win->UserPort);
            while ((m = GT_GetIMsg(win->UserPort))) {
                ULONG cl = m->Class, s = m->Seconds, u = m->Micros;
                UWORD code = m->Code;
                struct Gadget *gad = (struct Gadget *)m->IAddress;
                GT_ReplyIMsg(m);
                if (cl == IDCMP_REFRESHWINDOW) {
                    GT_BeginRefresh(win);
                    GT_EndRefresh(win, TRUE);
                } else if (cl == IDCMP_CLOSEWINDOW || (cl == IDCMP_RAWKEY && code == 0x45)) {
                    done = 1;
                } else if (cl == IDCMP_RAWKEY && (code == 0x44 || code == 0x43)) {
                    done = ok = 1;
                } else if (cl == IDCMP_GADGETUP) {
                    if (gad->GadgetID == GID_LIST) {    /* a second click on the name is OK */
                        if (code == sel && DoubleClick(secs, micros, s, u)) done = ok = 1;
                        sel = code;
                        secs = s;
                        micros = u;
                    } else if (gad->GadgetID == GID_OK) done = ok = 1;
                    else if (gad->GadgetID == GID_CANCEL) done = 1;
                }
            }
        }
        CloseWindow(win);
    }
    FreeGadgets(glist);
    FreeVisualInfo(vi);
    return ok && sel < n ? sel : -1;
}
