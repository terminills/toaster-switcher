/* The requesters of the Editor's ARexx functions (REQ_TELL, REQ_BUTTONS...), the list the Tools and Programs
 * buttons show and the file requester, in windows over w. All but the notice and the file requester are modal:
 * the switcher waits while one is open. */
#ifndef REXXREQ_H_INCLUDED
#define REXXREQ_H_INCLUDED
#include <exec/types.h>
#include <stddef.h>
struct Window;
struct filereq;

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
/* A file requester on dir, starting with the name file. It is not waited for: the caller steps it from its own
 * loop, with what it got from waiting for req_file_sigmask (not waiting at all while req_file_nowait). */
enum { REQF_SAVE = 1, REQF_MULTI = 2, REQF_CENTRE = 4 };
struct filereq *req_file_open(struct Window *w, const char *title, const char *dir, const char *file, int flags);
ULONG req_file_sigmask(const struct filereq *q);
int req_file_nowait(const struct filereq *q);
/* 0 while it is up, then 1 with a choice or -1 cancelled. */
int req_file_step(struct filereq *q, ULONG sigs);
/* The path of the nth file chosen (0 the only one without REQF_MULTI) in out; 0 when there is none. */
int req_file_name(const struct filereq *q, int n, char *out, size_t size);
/* One that is still up goes as if cancelled. */
void req_file_close(struct filereq *q);
/* One of n names: its index, -1 when cancelled. */
int req_choose(struct Window *w, const char *title, const char *const *names, int n);
#endif
