/*
A stand-in for Q3Remaster.exe in tools/q3core/test_updater.py: what it does is the first word of
fake_game.txt beside it. "ok": write .update\started (the menu opened) and exit; "crash": exit 3 at once;
"hang": never reach the menu. Every start appends its text to fake_game.log, so the test can tell
which build ran.
*/
#include <stdio.h>
#include <string.h>
#include <windows.h>

int main(void) {
	char dir[MAX_PATH], path[MAX_PATH], mode[64] = "";
	FILE *f;
	char *slash;
	GetModuleFileNameA(NULL, dir, MAX_PATH);
	slash = strrchr(dir, '\\');
	if (slash)
		*slash = 0;
	snprintf(path, sizeof(path), "%s\\fake_game.txt", dir);
	f = fopen(path, "rb");
	if (f) {
		if (fscanf(f, "%63s", mode) != 1)
			mode[0] = 0;
		fclose(f);
	}
	snprintf(path, sizeof(path), "%s\\fake_game.log", dir);
	f = fopen(path, "ab");
	if (f) {
		fprintf(f, "%s\n", mode);
		fclose(f);
	}
	if (!strcmp(mode, "crash"))
		return 3;
	if (!strcmp(mode, "hang")) {
		Sleep(INFINITE);
		return 0;
	}
	snprintf(path, sizeof(path), "%s\\.update\\started", dir);
	f = fopen(path, "wb");
	if (f) {
		fputs("ok", f);
		fclose(f);
	}
	return 0;
}
