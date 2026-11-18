/* The Framestore (see framestore.h). */
#include <stdlib.h>
#include <string.h>
#include "framestore.h"

void fs_init(struct framestore *fs, int w, int h)
{
    memset(fs, 0, sizeof(*fs));
    fs->w = w;
    fs->h = h;
}

void fs_free(struct framestore *fs)
{
    int s;
    for (s = 0; s < FS_SLOTS; s++) free(fs->still[s]);
    memset(fs->still, 0, sizeof(fs->still));
    free(fs->dv2);
    fs->dv2 = NULL;
}

static uint32_t *slot_buf(struct framestore *fs, int s)
{
    if (!fs->still[s]) fs->still[s] = malloc((size_t)fs->w * fs->h * 4);
    return fs->still[s];
}

int fs_freeze(struct framestore *fs, const uint32_t *live)
{
    uint32_t *b = slot_buf(fs, fs->cur);
    if (!b) return -1;
    memcpy(b, live, (size_t)fs->w * fs->h * 4);
    fs->frozen = 1;
    return 0;
}

void fs_show(const struct framestore *fs, uint32_t *out)
{
    const uint32_t *b = fs->still[fs->cur];
    int y;
    if (!b) {
        memset(out, 0, (size_t)fs->w * fs->h * 4);
        return;
    }
    for (y = 0; y < fs->h; y++)
        memcpy(out + (size_t)y * fs->w, b + (size_t)(fs->stable[fs->cur] ? y & ~1 : y) * fs->w, (size_t)fs->w * 4);
}

int fs_load(struct framestore *fs, int s, const uint32_t *rgb, int pw, int ph)
{
    uint32_t *b;
    int x, y;
    if (s < 0 || s >= FS_SLOTS || pw <= 0 || ph <= 0 || !(b = slot_buf(fs, s))) return -1;
    for (y = 0; y < fs->h; y++)
        for (x = 0; x < fs->w; x++)
            b[(size_t)y * fs->w + x] = rgb[(size_t)(y * ph / fs->h) * pw + x * pw / fs->w];
    fs->stable[s] = 0;
    return 0;
}

void fs_page(struct framestore *fs, uint32_t *rgb, int dv1)
{
    if (dv1) {
        if (!fs_load(fs, fs->cur, rgb, fs->w, fs->h)) fs->frozen = 1;
        free(rgb);
    } else {
        free(fs->dv2);
        fs->dv2 = rgb;
    }
}

void fs_live(struct framestore *fs)
{
    fs->frozen = 0;
    free(fs->dv2);
    fs->dv2 = NULL;
}
