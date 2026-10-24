/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code (via ioquake3's sys_main.c).

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.
===========================================================================
*/
/* q3core's replacement for ioquake3's sys/sys_main.c. The engine runs inside Godot, so there
 * is no main(), no console and no exit(): Sys_Print goes to Godot's output, and Sys_Error /
 * Sys_Quit longjmp back to the host call that was running (q3_host.c) instead of ending the
 * process. Everything else is sys_main.c's code. */
#include <sys/stat.h>

#include "../../thirdparty/ioq3/code/sys/sys_local.h"
#include "../../thirdparty/ioq3/code/sys/sys_loadlib.h"
#include "../../thirdparty/ioq3/code/qcommon/q_shared.h"
#include "../../thirdparty/ioq3/code/qcommon/qcommon.h"
#include "q3_engine.h"

static char binaryPath[MAX_OSPATH] = { 0 };
static char installPath[MAX_OSPATH] = { 0 };

void Sys_SetBinaryPath(const char *path) { Q_strncpyz(binaryPath, path, sizeof(binaryPath)); }
char *Sys_BinaryPath(void) { return binaryPath; }
void Sys_SetDefaultInstallPath(const char *path) { Q_strncpyz(installPath, path, sizeof(installPath)); }
char *Sys_DefaultInstallPath(void) { return *installPath ? installPath : Sys_Cwd(); }
char *Sys_DefaultAppPath(void) { return Sys_BinaryPath(); }
char *Sys_ConsoleInput(void) { return NULL; }
char *Sys_GetClipboardData(void) { return NULL; }
void Sys_InitPIDFile(const char *gamedir) { (void)gamedir; }
void Sys_RemovePIDFile(const char *gamedir) { (void)gamedir; }
qboolean Sys_OpenFolderInFileManager(const char *path, qboolean create) { (void)path; (void)create; return qfalse; }
cpuFeatures_t Sys_GetProcessorFeatures(void) { return 0; }

void Sys_Init(void) {
	Cvar_Set("arch", OS_STRING " " ARCH_STRING);
	Cvar_Set("username", Sys_GetCurrentUser());
}

void Sys_Print(const char *msg) {
	CON_LogWrite(msg);
	q3_engine_print(msg, 0);
}

/* "quit" from the console, or the end of an ERR_FATAL: back to Godot, engine stopped. */
void Sys_Quit(void) {
	q3_engine_fatal("quit");
}

void Sys_Error(const char *error, ...) {
	va_list argptr;
	char string[1024];
	va_start(argptr, error);
	Q_vsnprintf(string, sizeof(string), error, argptr);
	va_end(argptr);
	q3_engine_fatal(string);
}

#ifndef _WIN32
#include <unistd.h>
/* sys_unix.c's Sys_PlatformInit names it (sys_main.c has it; q3_host.c never runs that init, so no handler is installed) */
void Sys_SigHandler(int signal) {
	_exit(128 + signal);
}
#endif

int Sys_FileTime(char *path) {
	struct stat buf;
	if (stat(path, &buf) == -1)
		return -1;
	return buf.st_mtime;
}

void Sys_UnloadDll(void *dllHandle) {
	if (!dllHandle) {
		Com_Printf("Sys_UnloadDll(NULL)\n");
		return;
	}
	Sys_UnloadLibrary(dllHandle);
}

void *Sys_LoadDll(const char *name, qboolean useSystemLib) {
	void *dllhandle = NULL;
	char libPath[MAX_OSPATH];
	if (!Sys_DllExtension(name)) {
		Com_Printf("Refusing to attempt to load library \"%s\": Extension not allowed.\n", name);
		return NULL;
	}
	if (useSystemLib)
		dllhandle = Sys_LoadLibrary(name);
	if (!dllhandle && *binaryPath) {
		Com_sprintf(libPath, sizeof(libPath), "%s%c%s", binaryPath, PATH_SEP, name);
		dllhandle = Sys_LoadLibrary(libPath);
	}
	if (!dllhandle)
		Com_Printf("Loading \"%s\" failed\n", name);
	return dllhandle;
}

/* The game module (qagamex86_64.dll), found by FS_FindVM in a search path's baseq3/. */
void *Sys_LoadGameDll(const char *name, vmMainProc *entryPoint, intptr_t (*systemcalls)(intptr_t, ...)) {
	void *libHandle;
	void (*dllEntry)(intptr_t (*syscallptr)(intptr_t, ...));

	if (!Sys_DllExtension(name)) {
		Com_Printf("Refusing to attempt to load library \"%s\": Extension not allowed.\n", name);
		return NULL;
	}
	Com_Printf("Loading DLL file: %s\n", name);
	libHandle = Sys_LoadLibrary(name);
	if (!libHandle) {
		Com_Printf("Sys_LoadGameDll(%s) failed:\n\"%s\"\n", name, Sys_LibraryError());
		return NULL;
	}
	dllEntry = Sys_LoadFunction(libHandle, "dllEntry");
	*entryPoint = Sys_LoadFunction(libHandle, "vmMain");
	if (!*entryPoint || !dllEntry) {
		Com_Printf("Sys_LoadGameDll(%s) failed to find vmMain function:\n\"%s\" !\n", name, Sys_LibraryError());
		Sys_UnloadLibrary(libHandle);
		return NULL;
	}
	dllEntry(systemcalls);
	return libHandle;
}
