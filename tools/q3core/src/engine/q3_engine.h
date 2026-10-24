/* Shared between q3_sys.c, q3_host.c and q3_replay.c (engine side only). */
#ifndef Q3_ENGINE_H
#define Q3_ENGINE_H

#include <setjmp.h>

void q3_engine_print(const char *text, int is_error);
/* An ERR_FATAL, Sys_Error or quit: records the message, stops the engine and longjmps back to
 * the host call that was running. Never returns. */
void q3_engine_fatal(const char *message);

/* q3_host.c: the guard's state */
extern int q3_ready;
extern char q3_last_error[1024];
extern jmp_buf q3_fatal_frame;
extern int q3_fatal_armed;
extern jmp_buf abortframe;        /* common.c */
extern char com_errorMessage[];   /* common.c */

/* Wraps engine code: returns `drop` after an ERR_DROP, `fatal` after an ERR_FATAL. */
#define GUARD(drop, fatal) \
	if (!q3_ready) return (fatal); \
	if (setjmp(q3_fatal_frame)) { q3_fatal_armed = 0; return (fatal); } \
	q3_fatal_armed = 1; \
	if (setjmp(abortframe)) { q3_fatal_armed = 0; Q_strncpyz(q3_last_error, com_errorMessage, sizeof(q3_last_error)); return (drop); }
/* The same for void entry points. */
#define GUARD_V() \
	if (!q3_ready) return; \
	if (setjmp(q3_fatal_frame)) { q3_fatal_armed = 0; return; } \
	q3_fatal_armed = 1; \
	if (setjmp(abortframe)) { q3_fatal_armed = 0; Q_strncpyz(q3_last_error, com_errorMessage, sizeof(q3_last_error)); return; }
#define UNGUARD() q3_fatal_armed = 0

/* q3_replay.c: the recorder, run after every server frame (inside the frame's guard) */
void q3_record_after_frame(void);

#endif
