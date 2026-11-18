/* The requesters of the Editor's ARexx functions (REQ_TELL, REQ_BUTTONS...) and the list the Tools and Programs
 * buttons show, in windows over w. All but the notice are modal: the switcher waits while one is open. */
#ifndef REXXREQ_H_INCLUDED
#define REXXREQ_H_INCLUDED
#include <stddef.h>
struct Window;

/* n lines of text with Continue (1) and Cancel (0). */
int req_tell(struct Window *w, const char *const *lines, int n);
/* n check boxes (up to 4) under title; states holds '1' or '0' for each and is changed. 0 when cancelled. */
int req_buttons(struct Window *w, const char *title, char *states, const char *const *labels, int n);
/* 0 when cancelled. */
int req_number(struct Window *w, const char *title, long *v, long min, long max);
int req_string(struct Window *w, const char *title, char *s, size_t size);
/* A time as HH:MM:SS:FF in tc (12 bytes), which holds the one to start with. 0 when cancelled. */
int req_time(struct Window *w, const char *title, char *tc);
/* A notice of n lines that stays until req_notice_close; a new one takes its place. */
void req_notice(struct Window *w, const char *const *lines, int n);
void req_notice_close(void);
/* A message in w's title bar, "" or NULL for the window's own title again. */
void req_message(struct Window *w, const char *text);
/* A file chosen from dir, starting with the name file: its path in out. 0 when cancelled. */
int req_file(struct Window *w, const char *title, const char *dir, const char *file, char *out, size_t size);
/* One of n names: its index, -1 when cancelled. */
int req_choose(struct Window *w, const char *title, const char *const *names, int n);
#endif
