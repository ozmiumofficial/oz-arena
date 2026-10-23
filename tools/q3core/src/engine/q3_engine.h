/* Shared between q3_sys.c and q3_host.c (engine side only). */
#ifndef Q3_ENGINE_H
#define Q3_ENGINE_H

void q3_engine_print(const char *text, int is_error);
/* An ERR_FATAL, Sys_Error or quit: records the message, stops the engine and longjmps back to
 * the host call that was running. Never returns. */
void q3_engine_fatal(const char *message);

#endif
