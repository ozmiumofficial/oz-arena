#pragma once
// Native crash dumps for the whole game process (Diagnostics, scripts/diagnostics/diagnostics.gd).
// Plain C, so neither windows.h nor id's headers ever meet godot-cpp.
//
// Godot's own Windows crash handler prints a backtrace to the log and then passes the exception on
// (EXCEPTION_CONTINUE_SEARCH), so it reaches the top-level filter installed here, which writes a
// minidump and a short text note. abort() (SIGABRT) is caught too.

#ifdef __cplusplus
extern "C" {
#endif

// Starts writing crash_<date>_<pid>.dmp and .txt into dir (absolute, UTF-8) when the process dies.
// tag (UTF-8) goes into the note: the game version, the session id. Returns 1 when installed.
int q3_crash_install(const char *dir_utf8, const char *tag_utf8);

// Deliberate crashes, for testing the reports: 0 null write, 1 abort(), 2 stack overflow, 3 trap.
void q3_crash_test(int kind);

// When a process that is still running started (Unix seconds), or 0 when it isn't running. With
// the start time a reused pid can't pass for a game that crashed. (Godot's OS.is_process_running
// only knows processes Godot started itself.)
double q3_process_start_time(int pid);

#ifdef __cplusplus
}
#endif
