/* The outputs: places Program goes to for as long as they are on, each through a plug-in's tsoutput.h backend
 * ("net:8090": the backend's name and its argument). They run beside the clip recorder (record.h) and each other,
 * every one with Program's pictures at 30 a second and its sound. All but out_video and out_audio are for the task
 * that called out_init. */
#ifndef OUTPUTS_H
#define OUTPUTS_H
#include <stdint.h>

#define MAXOUTPUTS 4

enum { OUT_OFF, OUT_STARTING, OUT_ON, OUT_FAILED };

/* Program's pictures are w x h. Before anything else here. */
void out_init(int w, int h);
/* Switch output n on. 0 when it starts: whether its backend opened, and whether it still runs, is its state.
 * -1 when no plug-in has that backend. */
int out_start(int n, const char *spec);
void out_stop(int n);
void out_exit(void);            /* all of them off */
int out_state(int n);
int out_active(void);           /* one is on, or starting */
/* Program as it was just composed (from the interface) and its sound as it is played (from the sound thread). */
void out_video(const uint32_t *rgb);
void out_audio(const int16_t *pcm, int frames);
#endif
