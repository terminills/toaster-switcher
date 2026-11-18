/* Test tool: press the left button at (x1,y1), move to (x2,y2) in steps, release.
 * MouseDrag X1 Y1 X2 Y2 [STEPS=n] [NORELEASE] - coordinates on the default public screen. */
#include <exec/types.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <string.h>

static struct IOStdReq *io;

static void send(struct InputEvent *ie)
{
    io->io_Command = IND_WRITEEVENT;
    io->io_Data = ie;
    io->io_Length = sizeof(*ie);
    DoIO((struct IORequest *)io);
}

static void moveto(struct Screen *scr, LONG x, LONG y, UWORD qual)
{
    struct IEPointerPixel pp;
    struct InputEvent ie;
    memset(&ie, 0, sizeof(ie));
    pp.iepp_Screen = scr;
    pp.iepp_Position.X = x;
    pp.iepp_Position.Y = y;
    ie.ie_Class = IECLASS_NEWPOINTERPOS;
    ie.ie_SubClass = IESUBCLASS_PIXEL;
    ie.ie_Qualifier = qual;
    ie.ie_EventAddress = &pp;
    send(&ie);
}

static void button(BOOL down)
{
    struct InputEvent ie;
    memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_RAWMOUSE;
    ie.ie_Code = down ? IECODE_LBUTTON : (IECODE_LBUTTON | IECODE_UP_PREFIX);
    ie.ie_Qualifier = IEQUALIFIER_RELATIVEMOUSE | (down ? IEQUALIFIER_LEFTBUTTON : 0);
    send(&ie);
}

int main(void)
{
    SIPTR a[6] = { 0 };
    struct RDArgs *rda = ReadArgs((CONST_STRPTR)"X1/N/A,Y1/N/A,X2/N/A,Y2/N/A,STEPS/N,NORELEASE/S", a, NULL);
    struct MsgPort *port;
    struct Screen *scr;
    LONG x1, y1, x2, y2, steps, i;
    if (!rda) return RETURN_FAIL;
    x1 = *(LONG *)a[0]; y1 = *(LONG *)a[1]; x2 = *(LONG *)a[2]; y2 = *(LONG *)a[3];
    steps = a[4] ? *(LONG *)a[4] : 20;
    if (steps < 1) steps = 1;
    port = CreateMsgPort();
    io = (struct IOStdReq *)CreateIORequest(port, sizeof(*io));
    scr = LockPubScreen(NULL);
    if (!io || !scr || OpenDevice((CONST_STRPTR)"input.device", 0, (struct IORequest *)io, 0)) return RETURN_FAIL;
    moveto(scr, x1, y1, 0);
    Delay(2);
    button(TRUE);
    Delay(2);
    for (i = 1; i <= steps; i++) {
        moveto(scr, x1 + (x2 - x1) * i / steps, y1 + (y2 - y1) * i / steps, IEQUALIFIER_LEFTBUTTON);
        Delay(1);
    }
    if (!a[5]) {
        Delay(2);
        button(FALSE);
    }
    CloseDevice((struct IORequest *)io);
    UnlockPubScreen(NULL, scr);
    DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(port);
    FreeArgs(rda);
    return RETURN_OK;
}
