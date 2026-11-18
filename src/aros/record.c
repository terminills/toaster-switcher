/* Recording Program (see record.h): a sink (sink.h) that writes a file there is not yet. */
#include <proto/exec.h>
#include <proto/dos.h>
#include "sink.h"
#include "record.h"

#ifdef HAVE_FFMPEG
static const struct tso_backend *const backend = &tso_file;
#else
static const struct tso_backend *const backend = NULL;
#endif

static struct sink sink;

void rec_init(int w, int h)
{
    sink_init(&sink, w, h);
}

void rec_video(const uint32_t *rgb)
{
    sink_video(&sink, rgb);
}

void rec_audio(const int16_t *pcm, int frames)
{
    sink_audio(&sink, pcm, frames);
}

int rec_start(const char *name, long fields)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR win = me->pr_WindowPtr;
    BPTR l;
    int rc;
    if (!backend || !sink.ready) return -1;
    if (sink_active(&sink)) return REC_BUSY;
    if (!*name || fields < 0) return REC_BADPARAM;
    me->pr_WindowPtr = (APTR)-1;        /* a volume that is not there fails, it is not asked for */
    if ((l = Lock((CONST_STRPTR)name, ACCESS_READ))) {
        UnLock(l);
        me->pr_WindowPtr = win;
        return REC_EXISTS;
    }
    if ((rc = sink_start(&sink, backend, name, fields ? (fields + 1) / 2 : 0, 1))) {
        sink_stop(&sink);
        DeleteFile((CONST_STRPTR)name);
    }
    me->pr_WindowPtr = win;
    return rc ? REC_FAILED : REC_OKAY;
}

void rec_stop(void)
{
    sink_stop(&sink);
}

void rec_pause(int on)
{
    sink_pause(&sink, on);
}

int rec_status(void)
{
    if (!backend) return -1;
    switch (sink.state) {
    case SINK_OPENING:
    case SINK_RUNNING: return REC_BUSY;
    case SINK_FAILED: return REC_FULL;
    case SINK_NOTOPEN: return REC_FAILED;
    default: return REC_OKAY;
    }
}

int rec_active(void)
{
    return sink_active(&sink);
}
