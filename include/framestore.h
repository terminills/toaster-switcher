/* The Framestore: four stills grabbed from DV1 (FREEZE) and held on it until LIVE; saved and loaded as IFF ILBM. */
#ifndef FRAMESTORE_H
#define FRAMESTORE_H
#include <stdint.h>
#include <stddef.h>

#define FS_SLOTS 4

struct framestore {
    int w, h;
    uint32_t *still[FS_SLOTS];  /* w x h 0x00RRGGBB, NULL while empty */
    int stable[FS_SLOTS];       /* STABILIZE: shown with each odd line repeating the even one above */
    int cur;                    /* the slot FREEZE fills and DV1 shows */
    int frozen;                 /* DV1 shows the still instead of its stream */
};

void fs_init(struct framestore *fs, int w, int h);
void fs_free(struct framestore *fs);
/* FREEZE: the current slot takes live (w x h) and DV1 holds it. 0 on success. */
int fs_freeze(struct framestore *fs, const uint32_t *live);
/* What DV1 shows while frozen: the current slot's still (stabilized when set), black when it is empty. */
void fs_show(const struct framestore *fs, uint32_t *out);
/* A picture of any size into slot s, scaled to w x h. 0 on success. */
int fs_load(struct framestore *fs, int s, const uint32_t *rgb, int pw, int ph);
#endif
