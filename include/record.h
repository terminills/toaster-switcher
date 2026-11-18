/* Recording Program: what the Editor's RECORDCLIP, RECORDADD, RECORDPAUSE, RECORDSTOP and FLYERSTATUS do here. One
 * recording at a time, of Program's pictures as the monitor shows them (without the meters) at 30 a second, and of
 * its sound, through a tsoutput.h backend. The codes are the Flyer's (FERR_xxx in editorarexx.doc), which those
 * functions answer with. */
#ifndef RECORD_H
#define RECORD_H
#include <stdint.h>

enum {
    REC_OKAY = 0x00,
    REC_FAILED = 0x01,          /* the file can't be made */
    REC_BUSY = 0x02,            /* recording */
    REC_BADPARAM = 0x04,
    REC_FULL = 0x21,            /* writing failed */
    REC_CANTEXTEND = 0x29,
    REC_EXISTS = 0x2c
};

/* Program's pictures are w x h. Before anything else here. */
void rec_init(int w, int h);
/* Start recording into a file there is not yet: fields of it (two to a picture), or with 0 until rec_stop. A code
 * of the above, REC_OKAY when it runs; -1 when this build records nothing. */
int rec_start(const char *name, long fields);
/* End the recording, if one runs, and close its file. */
void rec_stop(void);
void rec_pause(int on);
/* REC_BUSY while it records, then how the last recording ended; -1 when this build records nothing. */
int rec_status(void);
int rec_active(void);
/* Program as it was just composed (from the interface) and its sound as it is played (from the sound thread). */
void rec_video(const uint32_t *rgb);
void rec_audio(const int16_t *pcm, int frames);
#endif
