/* The software Toaster card: routes switcher buses to input sources and keeps their latest frames. */
#ifndef INPUTS_H
#define INPUTS_H
#include <stdint.h>

/* Bus buttons of the Main/Preview rows, in FastGadget order */
enum { SLOT_IN1, SLOT_IN2, SLOT_IN3, SLOT_IN4, SLOT_DV1, SLOT_DV2, SLOT_BKG, NSLOTS };

#define SPECLEN 256             /* "backend:arg:source"; arg can be a file path */
#define NAMELEN 64
#define MAXSOURCES 32

struct inputs;

/* Start the capture process. specs[slot] is "backend:arg:source" (e.g. "vhi:uvc:1"), "" for nothing
 * (black), or NULL for automatic assignment. Frames are w x h. The capture process signals the calling task
 * with sigbit after each new frame and after each request. */
struct inputs *inputs_start(const char *const specs[NSLOTS], int w, int h, int sigbit);
void inputs_stop(struct inputs *in);
/* Which slots feed Main (program) and Preview. */
void inputs_route(struct inputs *in, int main_slot, int prvw_slot);
/* What each input's sound does: plays while it is on Program (crossfading with the transition), plays whatever is on
 * air, or never plays. */
enum { SOUND_FOLLOW, SOUND_ALWAYS, SOUND_OFF };
void inputs_sound(struct inputs *in, int slot, int mode);
/* Program's sound, frames (<= 4096) of 16-bit stereo at TSI_AUDIO_RATE: Main and Preview mixed by mix of mixmax as
 * their inputs' sound modes say, plus the SOUND_ALWAYS inputs on neither bus. Silence where none is at hand. */
void inputs_program_audio(struct inputs *in, int16_t *dst, int frames, int mix, int mixmax);
/* Which slot feeds the Key bus (Superimpose), -1 for none; one already on Main or Preview is shared. */
void inputs_route_key(struct inputs *in, int key_slot);
/* Copy the latest Key frame (w*h). */
void inputs_key(struct inputs *in, uint32_t *keybuf);
/* The latest frame of any slot (w*h): a bus's when one shows it, otherwise grabbed for it. 0 on success. */
int inputs_grab(struct inputs *in, int slot, uint32_t *buf);
/* Copy the latest Main and Preview frames (w*h each). Returns a count that changes when either is new. */
uint32_t inputs_frames(struct inputs *in, uint32_t *mainbuf, uint32_t *prvwbuf);

/* Names of the linked-in backends ("vhi", ...), NULL past the end. */
const char *inputs_backend(int index);
/* What a slot shows ("UGREEN Camera 2K", "black") and its spec ("" = black); valid once started. */
const char *inputs_slot_name(struct inputs *in, int slot);
const char *inputs_slot_spec(struct inputs *in, int slot);
/* Every slot has been through its opening at the start; until then the ones not reached yet are named "black". */
int inputs_ready(struct inputs *in);
/* The slot has a spec whose source did not open (a camera that is unplugged, a plug-in that is not there). */
int inputs_slot_missing(struct inputs *in, int slot);
/* Give a slot a new spec ("" = black). Done by the capture process; returns 0 when the source opened. */
int inputs_set(struct inputs *in, int slot, const char *spec);
/* Sources of backend/arg (e.g. "vhi", "uvc") into names; returns their count, -1 if it won't open. */
int inputs_sources(struct inputs *in, const char *backend, const char *arg, char names[][NAMELEN], int max);
/* Close what a plug-in has open but no input shows (it was only listed, or switched away from); it opens again when
 * asked for, and until then its plug-in is not held by it. Done by the capture process; returns how many it closed. */
int inputs_release_plugins(struct inputs *in);
/* Per source context since the last call: grab calls, frames delivered, ms spent in grab (one line each). */
int inputs_stats(struct inputs *in, char *buf, int len);
#endif
