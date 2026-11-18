/* The Framestore: four stills grabbed from DV1 (FREEZE) and held on it until LIVE; saved and loaded as IFF ILBM.
 * A frame page of the CG goes into it too, or is held on DV2 while DV1 is on Main (fs_page). */
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
    uint32_t *dv2;              /* w x h held on DV2 instead of its stream, NULL for none */
};

void fs_init(struct framestore *fs, int w, int h);
void fs_free(struct framestore *fs);
/* FREEZE: the current slot takes live (w x h) and DV1 holds it. 0 on success. */
int fs_freeze(struct framestore *fs, const uint32_t *live);
/* What DV1 shows while frozen: the current slot's still (stabilized when set), black when it is empty. */
void fs_show(const struct framestore *fs, uint32_t *out);
/* A picture of any size into slot s, scaled to w x h. 0 on success. */
int fs_load(struct framestore *fs, int s, const uint32_t *rgb, int pw, int ph);
/* A frame (w x h, taken over: it is freed here) for the input that is not on Main, as the Toaster loaded a frame
 * into the bank that was not on air: the current slot, held on DV1 (dv1 set), or held on DV2. */
void fs_page(struct framestore *fs, uint32_t *rgb, int dv1);
/* LIVE: DV1 and DV2 show their streams again. */
void fs_live(struct framestore *fs);
#endif
