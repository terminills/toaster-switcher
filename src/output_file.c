/* File output backend (see tsoutput.h): Program into a video file through FFmpeg, Motion JPEG pictures and 16-bit
 * sound in an AVI. Every picture is a whole one, so a recording that is cut short plays up to there, and what reads
 * AVI reads it (the file input backend among them). */
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/channel_layout.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tsoutput.h"

#define QUALITY 3               /* JPEG quantiser scale, 2 (finest) to 31 */

struct fileout {
    AVFormatContext *fmt;
    AVCodecContext *enc;
    AVStream *vst, *ast;
    struct SwsContext *sws;
    AVFrame *frame;
    AVPacket *pkt;
    int w, h, header, failed;
    int64_t npics, nsound;      /* pictures and frames of sound written */
};

static int file_close(void *ctx);

static void *file_open(const char *arg, int w, int h, int fps)
{
    struct fileout *c = calloc(1, sizeof(*c));
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    const int *coef = sws_getCoefficients(SWS_CS_DEFAULT);
    AVCodecParameters *par;
    char url[1024];
    if (!c) return NULL;
    c->w = w;
    c->h = h;
    snprintf(url, sizeof(url), "file:%s", arg);
    if (!codec || avformat_alloc_output_context2(&c->fmt, NULL, "avi", url) < 0) goto fail;
    if (!(c->enc = avcodec_alloc_context3(codec))) goto fail;
    c->enc->width = w;
    c->enc->height = h;
    c->enc->pix_fmt = AV_PIX_FMT_YUV420P;
    c->enc->color_range = AVCOL_RANGE_JPEG;
    c->enc->time_base = (AVRational){ 1, fps };
    c->enc->framerate = (AVRational){ fps, 1 };
    c->enc->flags |= AV_CODEC_FLAG_QSCALE;
    c->enc->global_quality = FF_QP2LAMBDA * QUALITY;
    c->enc->thread_count = 1;
    if (avcodec_open2(c->enc, codec, NULL) < 0) goto fail;
    if (!(c->vst = avformat_new_stream(c->fmt, NULL)) ||
        avcodec_parameters_from_context(c->vst->codecpar, c->enc) < 0) goto fail;
    c->vst->time_base = c->enc->time_base;
    c->vst->avg_frame_rate = c->enc->framerate;
    if (!(c->ast = avformat_new_stream(c->fmt, NULL))) goto fail;
    par = c->ast->codecpar;
    par->codec_type = AVMEDIA_TYPE_AUDIO;
    par->codec_id = AV_CODEC_ID_PCM_S16LE;
    par->format = AV_SAMPLE_FMT_S16;
    par->sample_rate = TSO_AUDIO_RATE;
    av_channel_layout_default(&par->ch_layout, 2);
    par->bits_per_coded_sample = 16;
    par->block_align = 4;
    par->bit_rate = (int64_t)TSO_AUDIO_RATE * 32;
    c->ast->time_base = (AVRational){ 1, TSO_AUDIO_RATE };
    if (!(c->frame = av_frame_alloc()) || !(c->pkt = av_packet_alloc())) goto fail;
    c->frame->format = c->enc->pix_fmt;
    c->frame->width = w;
    c->frame->height = h;
    c->frame->color_range = AVCOL_RANGE_JPEG;
    if (av_frame_get_buffer(c->frame, 0) < 0) goto fail;
    /* without SWS_ACCURATE_RND a build with inline assembler and no x86asm writes no chroma */
    c->sws = sws_getContext(w, h, AV_PIX_FMT_BGR0, w, h, c->enc->pix_fmt, SWS_BILINEAR | SWS_ACCURATE_RND, NULL, NULL,
                            NULL);
    if (!c->sws) goto fail;
    sws_setColorspaceDetails(c->sws, coef, 1, coef, 1, 0, 1 << 16, 1 << 16);    /* JPEG's full range */
    if (avio_open(&c->fmt->pb, url, AVIO_FLAG_WRITE) < 0 || avformat_write_header(c->fmt, NULL) < 0) goto fail;
    c->header = 1;
    return c;
fail:
    file_close(c);
    return NULL;
}

/* What the encoder has ready into the file. */
static int drain(struct fileout *c)
{
    for (;;) {
        int r = avcodec_receive_packet(c->enc, c->pkt);
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return 0;
        if (r < 0) return -1;
        av_packet_rescale_ts(c->pkt, c->enc->time_base, c->vst->time_base);
        c->pkt->stream_index = c->vst->index;
        if (av_interleaved_write_frame(c->fmt, c->pkt) < 0 || c->fmt->pb->error < 0) return -1;
    }
}

static int file_video(void *ctx, const uint32_t *rgb)
{
    struct fileout *c = ctx;
    const uint8_t *planes[4] = { (const uint8_t *)rgb };
    int stride[4] = { c->w * 4 };
    if (c->failed) return -1;
    c->failed = 1;
    if (av_frame_make_writable(c->frame) < 0) return -1;
    sws_scale(c->sws, planes, stride, 0, c->h, c->frame->data, c->frame->linesize);
    c->frame->pts = c->npics++;
    c->frame->quality = c->enc->global_quality;
    if (avcodec_send_frame(c->enc, c->frame) < 0 || drain(c)) return -1;
    c->failed = 0;
    return 0;
}

static int file_audio(void *ctx, const int16_t *pcm, int frames)
{
    struct fileout *c = ctx;
    AVRational rate = { 1, TSO_AUDIO_RATE };
    if (c->failed) return -1;
    c->failed = 1;
    if (av_new_packet(c->pkt, frames * 4) < 0) return -1;
#if AV_HAVE_BIGENDIAN
    {
        int i;
        for (i = 0; i < frames * 2; i++) {
            c->pkt->data[i * 2] = pcm[i] & 255;
            c->pkt->data[i * 2 + 1] = (pcm[i] >> 8) & 255;
        }
    }
#else
    memcpy(c->pkt->data, pcm, frames * 4);
#endif
    c->pkt->stream_index = c->ast->index;
    c->pkt->pts = c->pkt->dts = av_rescale_q(c->nsound, rate, c->ast->time_base);
    c->pkt->duration = av_rescale_q(frames, rate, c->ast->time_base);
    c->pkt->flags |= AV_PKT_FLAG_KEY;
    c->nsound += frames;
    if (av_interleaved_write_frame(c->fmt, c->pkt) < 0 || c->fmt->pb->error < 0) return -1;
    c->failed = 0;
    return 0;
}

static int file_close(void *ctx)
{
    struct fileout *c = ctx;
    int rc;
    if (!c) return -1;
    rc = c->failed || !c->header ? -1 : 0;
    if (c->header) {
        if (!rc && (avcodec_send_frame(c->enc, NULL) < 0 || drain(c))) rc = -1;
        if (av_write_trailer(c->fmt) < 0) rc = -1;      /* the index: without it the file is still read */
    }
    if (c->fmt && c->fmt->pb) {
        avio_flush(c->fmt->pb);
        if (c->fmt->pb->error < 0) rc = -1;
        if (avio_closep(&c->fmt->pb) < 0) rc = -1;
    }
    sws_freeContext(c->sws);
    av_frame_free(&c->frame);
    av_packet_free(&c->pkt);
    avcodec_free_context(&c->enc);
    if (c->fmt) avformat_free_context(c->fmt);
    free(c);
    return rc;
}

const struct tso_backend tso_file = { TSO_API_VERSION, "file", file_open, file_video, file_audio, file_close };
