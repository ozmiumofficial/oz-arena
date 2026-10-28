/*
Q3Updater.exe: installs a downloaded update once the game has exited (UPDATES_PLAN.md, U4).

  Q3Updater.exe <game dir> <game pid> <relaunch 0|1> <game exe name>
  Q3Updater.exe --rollback <game dir>

The game (Q3UpdateInstall) downloads every changed file into <game dir>\.update\stage\ (checked against
the signed manifest's SHA-256s), writes .update\apply.txt (the release tag, then one relative path per
line), copies this program to .update\ and starts the copy, then quits. This program:

  1. waits for the game's process to end (2 minutes at most),
  2. moves each target file to .update\backup\ and the staged file into its place (same volume, so each
     move is a rename); on any failure it puts back what it moved and stops,
  3. writes .update\applied ("relaunch" or "quit"),
  4. with relaunch 1: starts the game and waits for it to write .update\started (Q3UpdateInstall does
     when the main menu opens). If the new build exits first, or never gets there in 3 minutes, the
     old files go back and the old build starts. Once it started, the backup and stage go.
     With relaunch 0 (installed at quit) the game itself clears the backup on its next good start.

--rollback puts the backup back by hand (for an update applied at quit whose build won't start).
Everything it does goes to .update\updater.log. Plain Win32, no CRT console: it runs with no window.
*/
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define MAX_FILES 2000
#define WAIT_EXIT_MS (2 * 60 * 1000)
#define WAIT_START_MS (3 * 60 * 1000)

static wchar_t g_dir[MAX_PATH];        /* the game folder */
static wchar_t g_upd[MAX_PATH];        /* <game>\.update */
static FILE *g_log;

static wchar_t *g_paths[MAX_FILES];    /* relative paths, backslashes */
static int g_had[MAX_FILES];           /* the target existed before (so it has a backup) */
static int g_moved[MAX_FILES];         /* installed */
static int g_count;

static void logf_(const char *fmt, ...) {
	va_list ap;
	SYSTEMTIME t;
	if (!g_log)
		return;
	GetLocalTime(&t);
	fprintf(g_log, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
	va_start(ap, fmt);
	vfprintf(g_log, fmt, ap);
	va_end(ap);
	fputc('\n', g_log);
	fflush(g_log);
}

static void join(wchar_t *out, const wchar_t *a, const wchar_t *b) {
	swprintf(out, MAX_PATH, L"%ls\\%ls", a, b);
}

/* every folder above `path` */
static void make_parents(const wchar_t *path) {
	wchar_t buf[MAX_PATH];
	wchar_t *p;
	wcsncpy(buf, path, MAX_PATH - 1);
	buf[MAX_PATH - 1] = 0;
	for (p = buf + 3; *p; p++) {
		if (*p == L'\\') {
			*p = 0;
			CreateDirectoryW(buf, NULL);
			*p = L'\\';
		}
	}
}

static void delete_tree(const wchar_t *dir) {
	wchar_t pat[MAX_PATH], sub[MAX_PATH];
	WIN32_FIND_DATAW fd;
	HANDLE h;
	join(pat, dir, L"*");
	h = FindFirstFileW(pat, &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
				continue;
			join(sub, dir, fd.cFileName);
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				delete_tree(sub);
			else {
				SetFileAttributesW(sub, FILE_ATTRIBUTE_NORMAL);
				DeleteFileW(sub);
			}
		} while (FindNextFileW(h, &fd));
		FindClose(h);
	}
	RemoveDirectoryW(dir);
}

static int exists(const wchar_t *path) {
	return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

/* a rename that retries for a few seconds: antivirus scanners and the dying game hold files briefly */
static int move(const wchar_t *from, const wchar_t *to) {
	int i;
	make_parents(to);
	for (i = 0; i < 50; i++) {
		if (MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH))
			return 1;
		Sleep(100);
	}
	logf_("move failed (%lu): %ls -> %ls", GetLastError(), from, to);
	return 0;
}

static void write_text(const wchar_t *name, const char *text) {
	wchar_t p[MAX_PATH];
	FILE *f;
	join(p, g_upd, name);
	f = _wfopen(p, L"wb");
	if (f) {
		fputs(text, f);
		fclose(f);
	}
}

/* apply.txt: the tag, then one relative path ("a/b.dll") per line; refuses anything that leaves the folder */
static int read_list(void) {
	wchar_t p[MAX_PATH];
	char line[1024];
	FILE *f;
	int first = 1;
	join(p, g_upd, L"apply.txt");
	f = _wfopen(p, L"rb");
	if (!f) {
		logf_("no apply.txt");
		return 0;
	}
	while (fgets(line, sizeof(line), f)) {
		size_t n = strlen(line);
		wchar_t w[MAX_PATH];
		int i;
		while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = 0;
		if (first) {
			first = 0;
			logf_("update to %s", line);
			continue;
		}
		if (!n)
			continue;
		if (line[0] == '/' || strstr(line, "..") || strchr(line, ':') || strchr(line, '\\') || g_count >= MAX_FILES) {
			logf_("refused path %s", line);
			fclose(f);
			return 0;
		}
		if (!MultiByteToWideChar(CP_UTF8, 0, line, -1, w, MAX_PATH))
			continue;
		for (i = 0; w[i]; i++)
			if (w[i] == L'/')
				w[i] = L'\\';
		g_paths[g_count++] = _wcsdup(w);
	}
	fclose(f);
	return g_count > 0;
}

/* puts back every file moved so far: the backup over the new file, or the new file deleted */
static void roll_back(void) {
	int i;
	wchar_t target[MAX_PATH], backup[MAX_PATH], tmp[MAX_PATH];
	join(tmp, g_upd, L"backup");
	for (i = g_count - 1; i >= 0; i--) {
		if (!g_moved[i])
			continue;
		join(target, g_dir, g_paths[i]);
		join(backup, tmp, g_paths[i]);
		if (g_had[i])
			move(backup, target);
		else
			DeleteFileW(target);
		g_moved[i] = 0;
	}
	logf_("rolled back");
}

static int install(void) {
	int i;
	wchar_t target[MAX_PATH], staged[MAX_PATH], backup[MAX_PATH], sdir[MAX_PATH], bdir[MAX_PATH];
	join(sdir, g_upd, L"stage");
	join(bdir, g_upd, L"backup");
	delete_tree(bdir);
	CreateDirectoryW(bdir, NULL);
	for (i = 0; i < g_count; i++) {
		join(target, g_dir, g_paths[i]);
		join(staged, sdir, g_paths[i]);
		join(backup, bdir, g_paths[i]);
		if (!exists(staged)) {
			logf_("missing staged file %ls", g_paths[i]);
			roll_back();
			return 0;
		}
		g_had[i] = exists(target);
		if (g_had[i] && !move(target, backup)) {
			roll_back();
			return 0;
		}
		g_moved[i] = 1;   /* from here a roll back restores the backup */
		if (!move(staged, target)) {
			roll_back();
			return 0;
		}
	}
	logf_("installed %d files", g_count);
	return 1;
}

static HANDLE launch(const wchar_t *exe) {
	wchar_t path[MAX_PATH], cmd[MAX_PATH + 4];
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	join(path, g_dir, exe);
	swprintf(cmd, MAX_PATH + 4, L"\"%ls\"", path);
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	if (!CreateProcessW(path, cmd, NULL, NULL, FALSE, 0, NULL, g_dir, &si, &pi)) {
		logf_("couldn't start %ls (%lu)", path, GetLastError());
		return NULL;
	}
	CloseHandle(pi.hThread);
	return pi.hProcess;
}

/* the backup put back by hand, for an update installed at quit */
static int manual_rollback(void) {
	wchar_t bdir[MAX_PATH];
	join(bdir, g_upd, L"backup");
	if (!read_list())
		return 1;
	for (int i = 0; i < g_count; i++) {
		wchar_t b[MAX_PATH];
		join(b, bdir, g_paths[i]);
		g_had[i] = exists(b);
		g_moved[i] = 1;
	}
	roll_back();
	return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show) {
	int argc;
	wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	wchar_t p[MAX_PATH];
	DWORD pid;
	int relaunch;
	const wchar_t *exe;
	HANDLE game;
	(void)inst; (void)prev; (void)cmdline; (void)show;

	if (argc == 3 && !wcscmp(argv[1], L"--rollback")) {
		GetFullPathNameW(argv[2], MAX_PATH, g_dir, NULL);
		join(g_upd, g_dir, L".update");
		join(p, g_upd, L"updater.log");
		g_log = _wfopen(p, L"ab");
		logf_("manual rollback");
		return manual_rollback();
	}
	if (argc != 5)
		return 2;
	GetFullPathNameW(argv[1], MAX_PATH, g_dir, NULL);
	join(g_upd, g_dir, L".update");
	pid = (DWORD)_wtoi(argv[2]);
	relaunch = _wtoi(argv[3]) != 0;
	exe = argv[4];
	if (wcschr(exe, L'\\') || wcschr(exe, L'/') || wcsstr(exe, L".."))
		return 2;
	join(p, g_upd, L"updater.log");
	g_log = _wfopen(p, L"ab");
	logf_("Q3Updater: game %ls, pid %lu, relaunch %d", g_dir, pid, relaunch);

	game = OpenProcess(SYNCHRONIZE, FALSE, pid);
	if (game) {
		if (WaitForSingleObject(game, WAIT_EXIT_MS) != WAIT_OBJECT_0) {
			logf_("the game didn't exit; nothing changed");
			return 1;
		}
		CloseHandle(game);
	}
	if (!read_list() || !install()) {
		if (relaunch)
			launch(exe);
		return 1;
	}
	join(p, g_upd, L"started");
	DeleteFileW(p);
	write_text(L"applied", relaunch ? "relaunch" : "quit");
	if (!relaunch) {
		logf_("installed at quit; the next start clears the backup");
		return 0;
	}

	game = launch(exe);
	if (game) {
		DWORD waited = 0;
		while (waited < WAIT_START_MS) {
			/* (its exit is checked after the marker: a build may write it and then quit) */
			int gone = WaitForSingleObject(game, 500) == WAIT_OBJECT_0;
			if (exists(p)) {
				wchar_t d[MAX_PATH];
				logf_("the new build started");
				CloseHandle(game);
				join(d, g_upd, L"backup");
				delete_tree(d);
				join(d, g_upd, L"stage");
				delete_tree(d);
				join(d, g_upd, L"apply.txt");
				DeleteFileW(d);
				join(d, g_upd, L"applied");
				DeleteFileW(d);
				return 0;
			}
			if (gone) {
				logf_("the new build exited before its menu opened");
				break;
			}
			waited += 500;
		}
		if (waited >= WAIT_START_MS) {
			logf_("the new build never reached its menu; closing it");
			TerminateProcess(game, 1);
			WaitForSingleObject(game, 10000);
		}
		CloseHandle(game);
	}
	roll_back();
	join(p, g_upd, L"applied");
	DeleteFileW(p);
	write_text(L"failed", "the update didn't start; the previous version was restored");
	launch(exe);
	return 1;
}
