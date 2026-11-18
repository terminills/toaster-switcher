/* ALGO croutons: the Toaster card's own transitions (pushes, pulls, expands, slats, wipes, fades), redrawn natively
 * from each crouton's AlgoFX parameter block (CrUD TAGS 0x1000001c). The card rendered these in hardware, so this
 * follows the parameters, not a software original. Big-endian words:
 *   header: {0, header bytes (10, 8 for fades, 88 positionable), 0, flags, objects...}, then blocks
 *           {id, tracks..., 0} up to a 0 id. Flags: 4 time runs backwards, 16 fade (block 23), 128 wipe is a cross,
 *           0x600 tiled image, 0x1800 mosaic (blocks 11, 12).
 *   track: values are 16.16 longs (L) unless noted, s = transition position 0..1
 *     2 {word}                          constant
 *     3 {L start, L delta, L -, L max}  start + delta s, at most max
 *     4 {ramp, L base}                  base + start .. base + start + delta, linear in depth (perspective)
 *     5 {L turns, L phase, ramp, L c}   c + ramp(s) sin(2 pi (phase + turns s))
 *     6 {ramp, from, to}                ramp over the window from..to (unsigned words, 0 = 1.0)
 *     7 {bytes, (time, word)..., -1}    keyframes
 *     8 {bytes, (time, L start, L delta)..., -1}   one ramp per time segment
 *   type 0 moves: X blocks 1.., Y blocks 10..; per axis {-, -, offset, crop lo, crop hi, source centre, clip lo, clip hi,
 *           dest centre, dest size}; dest = centre + offset + (src - source centre) size / (crop hi - crop lo);
 *           source units 192 x 240 of the frame, dest units 184 x 240 of the screen. Mode 2 moves B over A, 3 A off B,
 *           5 A then B through the effect (a guess: the outgoing picture to the midpoint, the incoming back), 6 A off a
 *           matte. Block 23 fades the moving picture (0..255).
 *   type 1 wipes, units 736 x 481: X blocks 13.., Y block 22..; {offset, placement (0 from the anchor, -1 back from it,
 *           32767 centred), clip lo, clip hi, anchor, size, 1 = B inside}.
 *   type 2 fades: block 23 level 0..255; flags 8 dips through the matte. */
#ifndef ALGOFX_H
#define ALGOFX_H
#include <stdint.h>
#include <stddef.h>

#define ALGO_MAXWORDS 512
#define ALGO_MAXBLK 24
#define ALGO_MAXPAR 16

struct algo_track {
    int kind;
    int off;                    /* first data word in algofx.w */
};

struct algo_block {
    int id, n;
    struct algo_track p[ALGO_MAXPAR];
};

struct algofx {
    int type, mode, flags, nobj, nblk;
    struct algo_block blk[ALGO_MAXBLK];
    int16_t w[ALGO_MAXWORDS];
};

/* Parse a parameter block; 0 when every track is understood. */
int algofx_parse(struct algofx *a, int type, int mode, const uint8_t *blob, size_t n);
/* Draw the transition at t of tmax into out from a (outgoing) and b (incoming), all w x h 0x00RRGGBB. */
void algofx_render(const struct algofx *a, int t, int tmax, const uint32_t *pa, const uint32_t *pb, uint32_t *out,
                   int w, int h, uint32_t matte);
#endif
