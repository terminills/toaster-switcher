/* Video inputs for the switcher: the part of the Toaster card that delivered video, as replaceable backends.
 *
 * A backend offers numbered sources (a camera, a capture dongle, a test pattern) and hands out frames as
 * 0x00RRGGBB pixels. Two are linked in (input_vhi.c: cameras and capture cards through VHI; input_file.c: video
 * files); others come from plug-ins (tsplugin.h) with the same table.
 *
 * All calls are made from the switcher's capture thread (a pthread), never from the interface, so a backend may
 * block (a USB grab, a network read) and may use the C library. grab returns nonzero while a source has no new
 * frame yet. */
#ifndef TSINPUT_H
#define TSINPUT_H
#include <stdint.h>

#define TSI_API_VERSION 2
#define TSI_AUDIO_RATE 48000           /* audio from backends: 16-bit stereo at this rate */

struct tsi_backend {
    uint32_t api_version;               /* TSI_API_VERSION */
    const char *name;                   /* short name used in input specs, e.g. "vhi" */
    /* Open the backend with its argument (for "vhi": the driver library name). NULL on failure. */
    void *(*open)(const char *arg);
    void (*close)(void *ctx);
    int (*count)(void *ctx);            /* number of sources */
    /* Human-readable name of a source into buf; 0 on success. */
    int (*source_name)(void *ctx, int source, char *buf, int len);
    /* Grab one frame of a source, scaled to w x h into dst (w*h pixels). Blocks; 0 on success. */
    int (*grab)(void *ctx, int source, uint32_t *dst, int w, int h);
    /* Sound that came with the frames grabbed so far, up to frames stereo frames into dst (16-bit, interleaved, at
     * TSI_AUDIO_RATE); returns how many. NULL for a backend without sound. Version 2. */
    int (*audio)(void *ctx, int source, int16_t *dst, int frames);
    /* Task priority of the worker that grabs from it (0 = as the switcher): a backend that spends its grabs
     * decoding (cameras sending JPEG) goes below so files, sound and the interface keep their pace. */
    int priority;
};

extern const struct tsi_backend tsi_vhi;
extern const struct tsi_backend tsi_file;       /* video files through FFmpeg; arg = the file's path */
#endif
