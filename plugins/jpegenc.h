/* A baseline JPEG encoder for the pictures a stream sends: 4:2:0, fixed tables, nothing to set. */
#ifndef JPEGENC_H
#define JPEGENC_H
#include <stddef.h>
#include <stdint.h>

/* A buffer of this size holds any picture of w x h. */
size_t jpeg_bound(int w, int h);
/* w*h pixels of 0x00RRGGBB as a JFIF picture into out (jpeg_bound(w, h) bytes); returns its length. */
size_t jpeg_encode(const uint32_t *rgb, int w, int h, uint8_t *out);
#endif
