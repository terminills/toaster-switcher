/* Video files as streams: FFmpeg reads and decodes the file, frames come out at their own times and the file loops;
 * its sound is decoded along with the packets the pictures need, resampled for the switcher's sound. One source per
 * file (the path is the backend's argument). Built where an FFmpeg is available (HAVE_FFMPEG). */
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include "inputs.h"
#include "tsinput.h"

struct filesrc {
    char path[SPECLEN];
    AVFormatContext *fmt;
    AVCodecContext *dec;
    struct SwsContext *sws;
    AVFrame *frame, *last;      /* the next decoded frame; the newest due one */
    AVPacket *pkt;
    int vs, drained, have;      /* video stream; end of file sent to the decoder; frame holds an undelivered frame */
    double tb, start, t0;       /* time base, first timestamp, wall clock of the file's start (< 0: not yet) */
    int as;                     /* sound stream, -1 for none */
    AVCodecContext *adec;
    SwrContext *swr;
    AVFrame *aframe;
    int an;                     /* frames in abuf */
    int16_t abuf[TSI_AUDIO_RATE * 2];   /* a second of decoded sound */
};

static void open_sound(struct filesrc *c);

static double now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

static void file_close(void *ctx)
{
    struct filesrc *c = ctx;
    if (!c) return;
    sws_freeContext(c->sws);
    swr_free(&c->swr);
    av_frame_free(&c->aframe);
    avcodec_free_context(&c->adec);
    av_frame_free(&c->frame);
    av_frame_free(&c->last);
    av_packet_free(&c->pkt);
    avcodec_free_context(&c->dec);
    avformat_close_input(&c->fmt);
    free(c);
}

static void *file_open(const char *arg)
{
    struct filesrc *c = calloc(1, sizeof(*c));
    char url[SPECLEN + 8];
    const AVCodec *codec = NULL;
    AVStream *st;
    if (!c) return NULL;
    strncpy(c->path, arg, sizeof(c->path) - 1);
    snprintf(url, sizeof(url), "file:%s", arg);        /* a volume name ("Work:") is not a protocol */
    if (avformat_open_input(&c->fmt, url, NULL, NULL) < 0 || avformat_find_stream_info(c->fmt, NULL) < 0 ||
        (c->vs = av_find_best_stream(c->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0)) < 0 ||
        !(c->dec = avcodec_alloc_context3(codec)))
        goto fail;
    st = c->fmt->streams[c->vs];
    if (avcodec_parameters_to_context(c->dec, st->codecpar) < 0) goto fail;
    c->dec->thread_count = 1;
    if (avcodec_open2(c->dec, codec, NULL) < 0 || !(c->frame = av_frame_alloc()) || !(c->last = av_frame_alloc()) ||
        !(c->pkt = av_packet_alloc()))
        goto fail;
    c->tb = av_q2d(st->time_base);
    c->start = st->start_time != AV_NOPTS_VALUE ? st->start_time * c->tb : 0;
    c->t0 = -1;
    open_sound(c);
    return c;
fail:
    file_close(c);
    return NULL;
}

/* Its sound, when it has one that decodes: stereo 16-bit at TSI_AUDIO_RATE. */
static void open_sound(struct filesrc *c)
{
    const AVCodec *codec = NULL;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    c->as = av_find_best_stream(c->fmt, AVMEDIA_TYPE_AUDIO, -1, c->vs, &codec, 0);
    if (c->as < 0) return;
    if (!(c->adec = avcodec_alloc_context3(codec)) ||
        avcodec_parameters_to_context(c->adec, c->fmt->streams[c->as]->codecpar) < 0 ||
        avcodec_open2(c->adec, codec, NULL) < 0 || !(c->aframe = av_frame_alloc()) ||
        swr_alloc_set_opts2(&c->swr, &stereo, AV_SAMPLE_FMT_S16, TSI_AUDIO_RATE, &c->adec->ch_layout,
                            c->adec->sample_fmt, c->adec->sample_rate, 0, NULL) < 0 || swr_init(c->swr) < 0) {
        swr_free(&c->swr);
        av_frame_free(&c->aframe);
        avcodec_free_context(&c->adec);
        c->as = -1;
    }
}

/* A sound packet into abuf; when abuf would overflow (nobody takes the sound) the oldest goes. */
static void decode_sound(struct filesrc *c, AVPacket *pkt)
{
    if (avcodec_send_packet(c->adec, pkt) < 0) return;
    while (!avcodec_receive_frame(c->adec, c->aframe)) {
        int max = swr_get_out_samples(c->swr, c->aframe->nb_samples), n;
        uint8_t *out;
        if (max > TSI_AUDIO_RATE) max = TSI_AUDIO_RATE;
        if (c->an + max > TSI_AUDIO_RATE) {
            int drop = c->an + max - TSI_AUDIO_RATE;
            memmove(c->abuf, c->abuf + drop * 2, (size_t)(c->an - drop) * 4);
            c->an -= drop;
        }
        out = (uint8_t *)(c->abuf + c->an * 2);
        n = swr_convert(c->swr, &out, max, (const uint8_t **)c->aframe->extended_data, c->aframe->nb_samples);
        if (n > 0) c->an += n;
    }
}

static int file_audio(void *ctx, int source, int16_t *dst, int frames)
{
    struct filesrc *c = ctx;
    int n = frames < c->an ? frames : c->an;
    if (source || n <= 0) return 0;
    memcpy(dst, c->abuf, (size_t)n * 4);
    memmove(c->abuf, c->abuf + n * 2, (size_t)(c->an - n) * 4);
    c->an -= n;
    return n;
}

static int file_count(void *ctx)
{
    (void)ctx;
    return 1;
}

static int file_source_name(void *ctx, int source, char *buf, int len)
{
    struct filesrc *c = ctx;
    const char *b = strrchr(c->path, '/');
    if (source) return -1;
    if (!b) b = strrchr(c->path, ':');
    strncpy(buf, b ? b + 1 : c->path, len - 1);
    buf[len - 1] = 0;
    return 0;
}

/* The next decoded frame into c->frame; at the end of the file, back to its start. 0 on success. */
static int next_frame(struct filesrc *c)
{
    int rewound = 0;
    for (;;) {
        int r = avcodec_receive_frame(c->dec, c->frame);
        if (!r) return 0;
        if (r == AVERROR_EOF) {
            if (rewound) return -1;
            av_seek_frame(c->fmt, c->vs, c->fmt->streams[c->vs]->start_time != AV_NOPTS_VALUE ?
                          c->fmt->streams[c->vs]->start_time : 0, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(c->dec);
            if (c->adec) avcodec_flush_buffers(c->adec);
            c->drained = 0;
            c->t0 = -1;
            rewound = 1;
            continue;
        }
        if (r != AVERROR(EAGAIN)) return -1;
        if (av_read_frame(c->fmt, c->pkt) < 0) {
            if (c->drained) return -1;
            avcodec_send_packet(c->dec, NULL);
            c->drained = 1;
            continue;
        }
        if (c->pkt->stream_index == c->vs) avcodec_send_packet(c->dec, c->pkt);
        else if (c->pkt->stream_index == c->as) decode_sound(c, c->pkt);
        av_packet_unref(c->pkt);
    }
}

/* The newest frame that is due: frames whose time passed while nobody asked (a late wake) are decoded and
 * dropped, their sound kept, so pictures stay on time and the sound flows. */
static int file_grab(void *ctx, int source, uint32_t *dst, int w, int h)
{
    struct filesrc *c = ctx;
    double wall = now();
    int got = 0;
    uint8_t *planes[1] = { (uint8_t *)dst };
    int stride[1] = { w * 4 };
    if (source) return -1;
    for (;;) {
        int64_t pts;
        double t;
        if (!c->have) {
            if (next_frame(c)) break;
            c->have = 1;
        }
        pts = c->frame->best_effort_timestamp;
        t = pts != AV_NOPTS_VALUE ? pts * c->tb - c->start : 0;
        if (c->t0 < 0) c->t0 = wall - t;
        if (wall < c->t0 + t) break;                    /* not due yet */
        if (wall > c->t0 + t + 1.0) c->t0 = wall - t;   /* far behind (a long stall): carry on from here */
        av_frame_unref(c->last);
        av_frame_move_ref(c->last, c->frame);
        c->have = 0;
        got = 1;
    }
    if (!got) return 1;
    c->sws = sws_getCachedContext(c->sws, c->last->width, c->last->height, c->last->format, w, h,
                                  AV_PIX_FMT_BGR0, SWS_BILINEAR, NULL, NULL, NULL);
    if (!c->sws) return -1;
    sws_scale(c->sws, (const uint8_t *const *)c->last->data, c->last->linesize, 0, c->last->height, planes, stride);
    return 0;
}

const struct tsi_backend tsi_file = {
    TSI_API_VERSION, "file", file_open, file_close, file_count, file_source_name, file_grab, file_audio, 1
};
