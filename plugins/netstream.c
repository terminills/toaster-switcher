/* Program as a live stream on the network: a small HTTP server that sends whoever asks a Matroska stream of the
 * pictures as JPEG and the sound as it is, for as long as they stay. Anything that plays "http://<address>:<port>/"
 * shows it: a media source in OBS, VLC, ffplay, mpv. Nothing is compressed over time, so on a local network it is
 * as good as the pictures are, and of little use beyond one. */
#define __BSDSOCKET_NOLIBBASE__     /* the socket library is the calling thread's own: in the context */
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tsplugin.h"
#include "jpegenc.h"

#define MAXCLIENTS 4
#define HEADROOM 32             /* before a picture or a block of sound in the buffer: what announces it */
#define REQUESTWAIT 5.0         /* seconds a client has for its request */
#define SENDWAIT 1.0            /* seconds a client may keep the stream waiting before it is let go */

struct client {
    int fd;                     /* -1: nobody */
    int streaming;              /* its request came and the stream's header went */
    double since;
    char req[1024];
    int nreq;
    long v0, a0;                /* the pictures and the sound before it came: its time starts at 0 */
};

struct net {
    struct Library *sockbase;   /* of the thread all calls come from */
    int lfd, w, h, fps;
    struct client cl[MAXCLIENTS];
    long pictures, sound;       /* handed in so far; sound in frames */
    int silent;                 /* no sound came with the first second: the stream has silence */
    uint8_t *buf;
    size_t size;
};

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

/* What Matroska is written with: an id as it is, a size or a number in as few bytes as hold it. */
static uint8_t *put_id(uint8_t *p, uint32_t id)
{
    int n;
    for (n = 3; n > 0 && !(id >> (n * 8)); n--)
        ;
    for (; n >= 0; n--) *p++ = id >> (n * 8);
    return p;
}

/* A size of four bytes: there is room for any, and it can be filled in after what it counts. */
static uint8_t *put_size(uint8_t *p, uint32_t size)
{
    *p++ = 0x10 | size >> 24;
    *p++ = size >> 16;
    *p++ = size >> 8;
    *p++ = size;
    return p;
}

static uint8_t *put_uint(uint8_t *p, uint32_t id, uint64_t v)
{
    int n;
    for (n = 7; n > 0 && !(v >> (n * 8)); n--)
        ;
    p = put_id(p, id);
    *p++ = 0x80 | (n + 1);
    for (; n >= 0; n--) *p++ = v >> (n * 8);
    return p;
}

static uint8_t *put_string(uint8_t *p, uint32_t id, const char *s)
{
    p = put_id(p, id);
    *p++ = 0x80 | strlen(s);
    memcpy(p, s, strlen(s));
    return p + strlen(s);
}

static uint8_t *put_float(uint8_t *p, uint32_t id, double d)
{
    uint64_t u;
    int n;
    memcpy(&u, &d, 8);
    p = put_id(p, id);
    *p++ = 0x88;
    for (n = 7; n >= 0; n--) *p++ = u >> (n * 8);
    return p;
}

/* An element that holds others: begin() leaves room for its size, end() fills it in. */
static uint8_t *begin(uint8_t *p, uint32_t id)
{
    return put_id(p, id) + 4;
}

static void end(uint8_t *start, uint8_t *p)
{
    put_size(start - 4, p - start);
}

/* What a stream starts with: the pictures' track and the sound's, and a segment without an end. */
static size_t header(const struct net *c, uint8_t *out)
{
    static const uint8_t unknown[8] = { 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    uint8_t *p = out, *a, *b, *d;
    a = p = begin(p, 0x1a45dfa3);                       /* EBML */
    p = put_uint(p, 0x4286, 1);
    p = put_uint(p, 0x42f7, 1);
    p = put_uint(p, 0x42f2, 4);
    p = put_uint(p, 0x42f3, 8);
    p = put_string(p, 0x4282, "matroska");
    p = put_uint(p, 0x4287, 2);
    p = put_uint(p, 0x4285, 2);
    end(a, p);
    p = put_id(p, 0x18538067);                          /* Segment */
    memcpy(p, unknown, 8);
    p += 8;
    a = p = begin(p, 0x1549a966);                       /* Info */
    p = put_uint(p, 0x2ad7b1, 1000000);                 /* times are in milliseconds */
    p = put_string(p, 0x4d80, "Video Toaster");
    p = put_string(p, 0x5741, "Video Toaster");
    end(a, p);
    a = p = begin(p, 0x1654ae6b);                       /* Tracks */
    b = p = begin(p, 0xae);
    p = put_uint(p, 0xd7, 1);
    p = put_uint(p, 0x73c5, 1);
    p = put_uint(p, 0x83, 1);                           /* pictures */
    p = put_uint(p, 0x9c, 0);
    p = put_string(p, 0x86, "V_MJPEG");
    p = put_uint(p, 0x23e383, 1000000000 / c->fps);
    d = p = begin(p, 0xe0);
    p = put_uint(p, 0xb0, c->w);
    p = put_uint(p, 0xba, c->h);
    end(d, p);
    end(b, p);
    b = p = begin(p, 0xae);
    p = put_uint(p, 0xd7, 2);
    p = put_uint(p, 0x73c5, 2);
    p = put_uint(p, 0x83, 2);                           /* sound */
    p = put_uint(p, 0x9c, 0);
    p = put_string(p, 0x86, "A_PCM/INT/LIT");
    d = p = begin(p, 0xe1);
    p = put_float(p, 0xb5, TSO_AUDIO_RATE);
    p = put_uint(p, 0x9f, 2);
    p = put_uint(p, 0x6264, 16);
    end(d, p);
    end(b, p);
    end(a, p);
    return p - out;
}

/* A cluster with one block of track, at ms: its beginning, into the HEADROOM bytes that end at data. Returns where
 * it starts. */
static uint8_t *announce(uint8_t *data, size_t len, int track, uint64_t ms)
{
    uint8_t head[HEADROOM], *p = head, *a;
    a = p = begin(p, 0x1f43b675);                       /* Cluster */
    p = put_uint(p, 0xe7, ms);
    p = put_id(p, 0xa3);                                /* SimpleBlock */
    p = put_size(p, len + 4);
    *p++ = 0x80 | track;
    *p++ = 0;                                           /* at the cluster's time */
    *p++ = 0;
    *p++ = 0x80;                                        /* complete by itself */
    end(a, p + len);
    memcpy(data - (p - head), head, p - head);
    return data - (p - head);
}

static void drop(struct net *c, struct client *cl)
{
    struct Library *SocketBase = c->sockbase;
    CloseSocket(cl->fd);
    cl->fd = -1;
}

/* All of data to the client, which may keep the stream waiting SENDWAIT; 0 when it went. */
static int send_all(struct net *c, struct client *cl, const uint8_t *data, size_t len)
{
    struct Library *SocketBase = c->sockbase;
    double until = now() + SENDWAIT;
    while (len) {
        LONG n = send(cl->fd, (APTR)data, len, 0);
        if (n > 0) {
            data += n;
            len -= n;
        } else if (n < 0 && (Errno() == EWOULDBLOCK || Errno() == EAGAIN)) {
            double left = until - now();
            struct timeval tv;
            fd_set set;
            if (left <= 0) return -1;
            FD_ZERO(&set);
            FD_SET(cl->fd, &set);
            tv.tv_sec = (long)left;
            tv.tv_usec = (long)((left - (long)left) * 1e6);
            if (WaitSelect(cl->fd + 1, NULL, &set, NULL, &tv, NULL) < 0) return -1;
        } else {
            return -1;
        }
    }
    return 0;
}

/* The block in the buffer (len bytes after the headroom) to everyone who is streaming, at each one's own time. */
static void send_block(struct net *c, size_t len, int track)
{
    int i;
    for (i = 0; i < MAXCLIENTS; i++) {
        struct client *cl = &c->cl[i];
        uint64_t ms;
        uint8_t *start;
        if (cl->fd < 0 || !cl->streaming) continue;
        ms = track == 1 ? (uint64_t)(c->pictures - cl->v0) * 1000 / c->fps :
                          (uint64_t)(c->sound - cl->a0) * 1000 / TSO_AUDIO_RATE;
        start = announce(c->buf + HEADROOM, len, track, ms);
        if (send_all(c, cl, start, c->buf + HEADROOM + len - start)) drop(c, cl);
    }
}

/* Who is new, and whose request is complete: those get the reply and the stream's header. */
static void attend(struct net *c)
{
    static const char reply[] = "HTTP/1.0 200 OK\r\nContent-Type: video/x-matroska\r\nCache-Control: no-cache\r\n"
                                "Connection: close\r\n\r\n";
    struct Library *SocketBase = c->sockbase;
    int i, fd;
    while ((fd = accept(c->lfd, NULL, NULL)) >= 0) {
        long one = 1;
        for (i = 0; i < MAXCLIENTS && c->cl[i].fd >= 0; i++)
            ;
        if (i == MAXCLIENTS) {
            CloseSocket(fd);
            continue;
        }
        IoctlSocket(fd, FIONBIO, (char *)&one);
        memset(&c->cl[i], 0, sizeof(c->cl[i]));
        c->cl[i].fd = fd;
        c->cl[i].since = now();
    }
    for (i = 0; i < MAXCLIENTS; i++) {
        struct client *cl = &c->cl[i];
        uint8_t head[512];
        LONG n;
        if (cl->fd < 0 || cl->streaming) continue;
        n = recv(cl->fd, cl->req + cl->nreq, sizeof(cl->req) - 1 - cl->nreq, 0);
        if (n > 0) cl->nreq += n;
        cl->req[cl->nreq] = 0;
        if (!strstr(cl->req, "\r\n\r\n") && !strstr(cl->req, "\n\n")) {
            if (n == 0 || (n < 0 && Errno() != EWOULDBLOCK && Errno() != EAGAIN) ||
                cl->nreq == (int)sizeof(cl->req) - 1 || now() - cl->since > REQUESTWAIT) drop(c, cl);
            continue;
        }
        if (send_all(c, cl, (const uint8_t *)reply, sizeof(reply) - 1) || send_all(c, cl, head, header(c, head))) {
            drop(c, cl);
            continue;
        }
        cl->streaming = 1;
        cl->v0 = c->pictures;
        cl->a0 = c->sound;
    }
}

static void *net_open(const char *arg, int w, int h, int fps)
{
    struct Library *SocketBase;
    struct sockaddr_in addr;
    struct net *c;
    char *e;
    long port = *arg ? strtol(arg, &e, 10) : 8090, one = 1;
    int i;
    if ((*arg && *e) || port < 1 || port > 65535 || fps < 1) return NULL;
    if (!(c = calloc(1, sizeof(*c)))) return NULL;
    c->w = w;
    c->h = h;
    c->fps = fps;
    c->lfd = -1;
    for (i = 0; i < MAXCLIENTS; i++) c->cl[i].fd = -1;
    c->size = jpeg_bound(w, h);
    if (c->size < TSO_AUDIO_RATE * 4) c->size = TSO_AUDIO_RATE * 4;
    c->buf = malloc(HEADROOM + c->size);
    SocketBase = c->sockbase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (c->buf && SocketBase && (c->lfd = socket(AF_INET, SOCK_STREAM, 0)) >= 0) {
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        setsockopt(c->lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        if (!bind(c->lfd, (struct sockaddr *)&addr, sizeof(addr)) && !listen(c->lfd, MAXCLIENTS) &&
            !IoctlSocket(c->lfd, FIONBIO, (char *)&one)) return c;
        CloseSocket(c->lfd);
    }
    if (SocketBase) CloseLibrary(SocketBase);
    free(c->buf);
    free(c);
    return NULL;
}

static int net_audio(void *ctx, const int16_t *pcm, int frames)
{
    struct net *c = ctx;
    if (c->silent) return 0;
    while (frames > 0) {
        int n = frames > (int)(c->size / 4) ? (int)(c->size / 4) : frames;
        memcpy(c->buf + HEADROOM, pcm, (size_t)n * 4);
        send_block(c, (size_t)n * 4, 2);
        c->sound += n;
        pcm += n * 2;
        frames -= n;
    }
    return 0;
}

static int net_video(void *ctx, const uint32_t *rgb)
{
    struct net *c = ctx;
    int i, any = 0;
    attend(c);
    for (i = 0; i < MAXCLIENTS; i++)
        if (c->cl[i].fd >= 0 && c->cl[i].streaming) any = 1;
    if (any) send_block(c, jpeg_encode(rgb, c->w, c->h, c->buf + HEADROOM), 1);
    c->pictures++;
    if (!c->sound && c->pictures == c->fps) c->silent = 1;     /* the Switcher plays no sound */
    if (c->silent) {                                            /* a player waits for the track it was promised */
        int n = TSO_AUDIO_RATE / c->fps;
        memset(c->buf + HEADROOM, 0, (size_t)n * 4);
        if (any) send_block(c, (size_t)n * 4, 2);
        c->sound += n;
    }
    return 0;
}

static int net_close(void *ctx)
{
    struct net *c = ctx;
    struct Library *SocketBase = c->sockbase;
    int i;
    for (i = 0; i < MAXCLIENTS; i++)
        if (c->cl[i].fd >= 0) CloseSocket(c->cl[i].fd);
    CloseSocket(c->lfd);
    CloseLibrary(SocketBase);
    free(c->buf);
    free(c);
    return 0;
}

static const struct tso_backend backend = { TSO_API_VERSION, "net", net_open, net_video, net_audio, net_close };

static const struct tsp_output outputs[] = {
    { &backend, "Network stream", "Port to serve the stream on\n(players open http://<this machine's address>:<port>/)", "8090" }
};

TSP_IDENTITY("netstream.tsplugin", 1, 0, "18.11.2026");

const struct tsp_plugin tsp_plugin = {
    TSP_API_VERSION, "Network stream", "Program as a live stream for OBS, VLC and other players on the network", NULL, 0,
    outputs, 1
};
