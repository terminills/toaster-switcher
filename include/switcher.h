/* Switcher interface state built on the original program's data (Switcher.main 4.06).
 * Offsets are NewTek's symbol values, recovered from the source/binary alignment (dis/aa/equates.tsv). */
#ifndef SWITCHER_H
#define SWITCHER_H
#include <stdint.h>
#include "hunkimg.h"
#include "effect.h"
#include "editbar.h"

#define DISPLAYWIDTH 768
#define DISPLAYHEIGHT 481
#define BYTESPERROW (DISPLAYWIDTH / 8)

/* struct Gadget (intuition) and FastGadget extension */
#define gg_NextGadget 0
#define gg_LeftEdge 4
#define gg_TopEdge 6
#define gg_Width 8
#define gg_Height 10
#define FG_ModeType 44
#define FG_WWidth 46
#define FG_Height 48
#define FG_Modulus 50
#define FG_Offset 52
#define FG_Data 56
#define FG_Function 68
#define FG_DispFlag 73
#define FG_IndexID 76
#define FG_HiLiteMask 78
#define FG_BorderCon 80

/* ToasterBase (DataPool) fields holding the FastGadget lists */
#define TB_SliceFGL 344
#define TB_LumKeyFGL 348
#define TB_MainFGL 352
#define TB_PrvwFGL 356
#define TB_GridSelFGL 360
#define TB_TransFGL 364
#define TB_TBarFGL 368
#define TB_FMCountFGL 372
#define TB_NumPadFGL 376
#define TB_ClipFGL 380
#define TB_MiscFGL 384
#define TB_BGColorFGL 786

enum { ROW_SLICE, ROW_LUMKEY, ROW_MAIN, ROW_PRVW, ROW_GRIDSEL, ROW_TRANS, ROW_TBAR, ROW_FMCOUNT, ROW_NUMPAD, ROW_CLIP,
       ROW_MISC, ROW_BGCOLOR, NROWS };

#define MAXFG 32
struct fgrow {
    uint32_t fg[MAXFG];         /* 68k addresses of the row's FastGadgets, in list order */
    int n;
    int hilite;                 /* index of the highlighted FastGadget, -1 none */
};

struct switcher {
    struct hunkimg img;
    uint32_t datapool;          /* ToasterBase: a5 of the original program */
    struct fgrow row[NROWS];
    int bgpopup;                /* background colour popup shown */
    int bgcolor;                /* BKG colour: index into ROW_BGCOLOR (1 BLACK ... 9 SNOW) */
    uint32_t translit;          /* ROW_TRANS buttons shown lit, one bit each (the Framestore's state) */
    int keyclip;                /* Superimpose clip level 0..255, shown in ROW_CLIP's CLIP_LEVEL box */
    int tbarpos;                /* T-bar handle offset from the top of its travel, 0..tbartravel */
    int tbartravel;             /* pixels the handle can move; 0 until sw_open found the T-bar */
    int tbarhandle;             /* rows of the T-bar picture that are the handle (the rest is stem) */
    int speed;                  /* 0 S, 1 M, 2 F */
    struct effect *fx;          /* the project's croutons, shown in the Editor grid */
    int nfx;
    int fxsel;                  /* selected crouton (the current effect), -1 none */
    int fxpicks;                /* counts the selections, for what a crouton does when it is chosen */
    int fxtop;                  /* first grid row shown */
    struct editbar bar;         /* the Editor's button bar */
    char inlabel[8][32];        /* sub labels under the input buttons (stream names), "" for none */
    uint32_t *rgb;              /* DISPLAYWIDTH x DISPLAYHEIGHT, 0x00RRGGBB */
};

int sw_open(struct switcher *sw, const uint8_t *exe, size_t n);
void sw_close(struct switcher *sw);
void sw_render(struct switcher *sw);
/* Rows of the T-bar and transition FastGadgets */
#define TBAR_FG 0               /* in ROW_TBAR: the T-bar picture */
#define TRANS_AUTO 0            /* in ROW_TRANS */
#define TRANS_TAKE 1
/* ROW_LUMKEY: Superimpose sources in slot order (1-4, DV1, DV2, BKG). ROW_CLIP: the key's mode and level. */
#define CLIP_LEVEL 0
#define CLIP_OFF 3
#define CLIP_BLACK 4            /* key out the dark parts of the source */
#define CLIP_WHITE 5            /* key out the light parts */
#define PRVW_DIVIDER 7          /* in ROW_PRVW: the PREVIEW divider; drawn LABEL_H lower to make room for the labels */
#define LABEL_H 11
#define TRANS_FREEZE 6          /* the FRAMESTORE box: FREEZE, LIVE, slots 1-4, STABILIZE, SAVE */
#define TRANS_LIVE 7
#define TRANS_FS1 8
#define TRANS_STABILIZE 12
#define TRANS_SAVE 13
#define TRANS_BGCOLOR 3         /* the box beside S M F naming the BKG colour; opens ROW_BGCOLOR */
#define BG_SNOW 9

/* BKG colour (0x00RRGGBB) and name for ROW_BGCOLOR entry i; SNOW is noise and returns -1. */
long sw_bgcolor(int i);
const char *sw_bgname(int i);
/* ROW_FMCOUNT holds the S, M and F buttons, in that order */

/* The Editor's crouton grid in the upper part of the display */
#define GRID_X 64
#define GRID_Y 40
#define GRID_PITCHX 81
#define GRID_PITCHY 51
#define GRID_COLS 8
#define GRID_ROWS 4

/* Crouton under (x,y): its index in fx[], or -1. */
int sw_hit_crouton(const struct switcher *sw, int x, int y);

/* FastGadget under (x,y): returns 1 and its row/index. */
int sw_hit(const struct switcher *sw, int x, int y, int *row, int *index);
/* Box of a FastGadget (screen coordinates of the interface display). */
void sw_box(const struct switcher *sw, int row, int index, int *x, int *y, int *w, int *h);
#endif
