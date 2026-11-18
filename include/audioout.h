/* Program sound: Main's sound, crossfaded into Preview's as the transition runs (audio follows video), to AHI. */
#ifndef AUDIOOUT_H
#define AUDIOOUT_H
struct inputs;
struct audioout;

/* Start playing; *mix (0..mixmax) is read for the crossfade, levels[0..1] get each block's left and right peak
 * (0..32767). NULL when there is no thread; without ahi.device nothing is heard, the levels and a recording still get the sound. */
struct audioout *audio_start(struct inputs *in, volatile int *mix, int mixmax, volatile int *levels);
void audio_stop(struct audioout *ao);
#endif
