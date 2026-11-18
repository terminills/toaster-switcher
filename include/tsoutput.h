/* Where Program goes besides the monitor: the counterpart of tsinput.h. A backend is opened for one recording or one
 * output and is given Program's pictures and its sound as they are made. One is linked in (output_file.c, the clip
 * recorder's); the outputs of the Inputs and Outputs window come from plug-ins (tsplugin.h).
 *
 * All calls for one recording or output, open and close too, come from one thread of its own (sink.c, a pthread), so
 * a backend may block and may use the C library. It gets fps pictures a second by the clock and the sound that goes
 * with them; a picture it takes long over makes the next ones late, not lost. */
#ifndef TSOUTPUT_H
#define TSOUTPUT_H
#include <stdint.h>

#define TSO_API_VERSION 1
#define TSO_AUDIO_RATE 48000            /* sound to backends: 16-bit stereo at this rate */

struct tso_backend {
    uint32_t api_version;               /* TSO_API_VERSION */
    const char *name;
    /* Open with the backend's argument (for "file": the path) for pictures of w x h, fps of them a second.
     * NULL on failure. */
    void *(*open)(const char *arg, int w, int h, int fps);
    /* The next picture: w*h pixels of 0x00RRGGBB. 0 on success. */
    int (*video)(void *ctx, const uint32_t *rgb);
    /* Sound that goes with the pictures: frames stereo frames (16-bit, interleaved). 0 on success. */
    int (*audio)(void *ctx, const int16_t *pcm, int frames);
    /* End the recording; 0 when all of it was written. */
    int (*close)(void *ctx);
};

extern const struct tso_backend tso_file;       /* a video file through FFmpeg; arg = the file's path */
#endif
