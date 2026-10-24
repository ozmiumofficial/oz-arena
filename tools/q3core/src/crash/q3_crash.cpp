#include "q3_crash.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

static wchar_t g_dir[MAX_PATH];
static char g_tag[256];
static LPTOP_LEVEL_EXCEPTION_FILTER g_prev = nullptr;
static volatile LONG g_written = 0;

struct DumpJob {
	EXCEPTION_POINTERS *ep;
	DWORD thread;
	const char *why;
};

static const char *code_name(DWORD code) {
	switch (code) {
		case EXCEPTION_ACCESS_VIOLATION: return "access violation";
		case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
		case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
		case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
		case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
		case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
		case EXCEPTION_BREAKPOINT: return "breakpoint (a fatal error trap)";
		case 0xC0000374: return "heap corruption";
		case 0xC0000409: return "stack buffer overrun";
	}
	return "exception";
}

// "q3core.windows.template_debug.x86_64.dll+0x1a2b3" for an address, or "?".
static void module_of(const void *addr, char *out, size_t n) {
	HMODULE mod = nullptr;
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				(LPCWSTR)addr, &mod)) {
		snprintf(out, n, "?");
		return;
	}
	wchar_t path[MAX_PATH];
	DWORD len = GetModuleFileNameW(mod, path, MAX_PATH);
	const wchar_t *name = path;
	for (DWORD i = 0; i < len; i++) {
		if (path[i] == L'\\' || path[i] == L'/') {
			name = path + i + 1;
		}
	}
	char name8[MAX_PATH * 3];
	WideCharToMultiByte(CP_UTF8, 0, name, -1, name8, sizeof(name8), nullptr, nullptr);
	snprintf(out, n, "%s+0x%llx", name8, (unsigned long long)((const char *)addr - (const char *)mod));
}

// Runs on its own thread, so a stack overflow on the crashing thread still gets its dump.
static DWORD WINAPI dump_thread(LPVOID p) {
	DumpJob *job = (DumpJob *)p;
	SYSTEMTIME t;
	GetLocalTime(&t);
	wchar_t base[MAX_PATH * 2];
	swprintf(base, MAX_PATH * 2, L"%ls\\crash_%04u-%02u-%02u_%02u.%02u.%02u_%lu", g_dir, t.wYear, t.wMonth, t.wDay,
			t.wHour, t.wMinute, t.wSecond, GetCurrentProcessId());
	wchar_t path[MAX_PATH * 2 + 8];
	swprintf(path, MAX_PATH * 2 + 8, L"%ls.dmp", base);
	BOOL ok = FALSE;
	HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (f != INVALID_HANDLE_VALUE) {
		MINIDUMP_EXCEPTION_INFORMATION mei;
		mei.ThreadId = job->thread;
		mei.ExceptionPointers = job->ep;
		mei.ClientPointers = FALSE;
		MINIDUMP_TYPE type = (MINIDUMP_TYPE)(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory |
				MiniDumpWithUnloadedModules | MiniDumpScanMemory);
		ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f, type, job->ep ? &mei : nullptr, nullptr, nullptr);
		CloseHandle(f);
	}
	swprintf(path, MAX_PATH * 2 + 8, L"%ls.txt", base);
	FILE *tf = _wfopen(path, L"w");
	if (tf) {
		fprintf(tf, "reason: %s\n", job->why);
		if (job->ep && job->ep->ExceptionRecord) {
			EXCEPTION_RECORD *r = job->ep->ExceptionRecord;
			char where[MAX_PATH * 3 + 32];
			module_of(r->ExceptionAddress, where, sizeof(where));
			fprintf(tf, "exception: 0x%08lx (%s)\n", r->ExceptionCode, code_name(r->ExceptionCode));
			fprintf(tf, "address: %p (%s)\n", r->ExceptionAddress, where);
			if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
				const char *op = r->ExceptionInformation[0] == 0 ? "reading" : (r->ExceptionInformation[0] == 1 ? "writing" : "executing");
				fprintf(tf, "access: %s 0x%llx\n", op, (unsigned long long)r->ExceptionInformation[1]);
			}
		}
		fprintf(tf, "thread: %lu\npid: %lu\ntag: %s\ndump: %s\n", job->thread, GetCurrentProcessId(), g_tag,
				ok ? "written" : "failed");
		fclose(tf);
	}
	return 0;
}

static void write_dump(EXCEPTION_POINTERS *ep, const char *why) {
	if (InterlockedExchange(&g_written, 1)) {
		return; // one dump per process
	}
	DumpJob job = {ep, GetCurrentThreadId(), why};
	HANDLE th = CreateThread(nullptr, 0, dump_thread, &job, 0, nullptr);
	if (th) {
		WaitForSingleObject(th, 60000);
		CloseHandle(th);
	}
}

static LONG WINAPI on_unhandled(EXCEPTION_POINTERS *ep) {
	write_dump(ep, "unhandled exception");
	return g_prev ? g_prev(ep) : EXCEPTION_CONTINUE_SEARCH;
}

static void on_abort(int) {
	write_dump(nullptr, "abort()");
}

extern "C" int q3_crash_install(const char *dir_utf8, const char *tag_utf8) {
	if (!MultiByteToWideChar(CP_UTF8, 0, dir_utf8, -1, g_dir, MAX_PATH)) {
		return 0;
	}
	for (wchar_t *c = g_dir; *c; c++) {
		if (*c == L'/') {
			*c = L'\\';
		}
	}
	snprintf(g_tag, sizeof(g_tag), "%s", tag_utf8 ? tag_utf8 : "");
	CreateDirectoryW(g_dir, nullptr);
	LPTOP_LEVEL_EXCEPTION_FILTER prev = SetUnhandledExceptionFilter(on_unhandled);
	if (prev != on_unhandled) {
		g_prev = prev; // installing twice (a second session id) keeps the original chain
	}
	signal(SIGABRT, on_abort);
	return 1;
}

#pragma warning(disable : 4717) // overflow() recurses forever on purpose
static int overflow(volatile int depth) {
	volatile char pad[4096];
	pad[0] = (char)depth;
	return overflow(depth + 1) + pad[0];
}

extern "C" void q3_crash_test(int kind) {
	switch (kind) {
		case 0: {
			volatile int *p = nullptr;
			*p = 1;
			break;
		}
		case 1:
			abort();
			break;
		case 2:
			overflow(0);
			break;
		case 3:
			__debugbreak();
			break;
	}
}

extern "C" double q3_process_start_time(int pid) {
	HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
	if (!h) {
		return 0;
	}
	double out = 0;
	DWORD code = 0;
	FILETIME created, exited, kernel, user;
	if (GetExitCodeProcess(h, &code) && code == STILL_ACTIVE && GetProcessTimes(h, &created, &exited, &kernel, &user)) {
		ULARGE_INTEGER t;
		t.LowPart = created.dwLowDateTime;
		t.HighPart = created.dwHighDateTime;
		out = (double)(t.QuadPart - 116444736000000000ULL) / 1e7; // 100 ns ticks since 1601 -> Unix seconds
	}
	CloseHandle(h);
	return out;
}

#else

#include <cstdlib>

extern "C" double q3_process_start_time(int) {
	return 0;
}

extern "C" int q3_crash_install(const char *, const char *) {
	return 0;
}

extern "C" void q3_crash_test(int kind) {
	if (kind == 1) {
		abort();
	}
	volatile int *p = nullptr;
	*p = 1;
}

#endif
