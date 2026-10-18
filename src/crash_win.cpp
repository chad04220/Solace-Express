// Crash reports on Windows: the handlers (crash.h). Everything the handler needs is set up at install, so writing the
// report only opens files and calls MiniDumpWriteDump (dbghelp.dll, loaded at install: a crash is no time to load one)
#include "crash.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

typedef BOOL(WINAPI* MiniDumpWriteDumpFn)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                          PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
static MiniDumpWriteDumpFn s_writeDump;
static char s_dir[MAX_PATH], s_log[MAX_PATH], s_version[96];
static volatile LONG s_writer = 0;   // the thread writing the report

static const char* codeName(DWORD c) {
  switch (c) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
    case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case 0xE0000001: return "std::terminate";
    case 0xE0000002: return "abort()";
    case 0xE0000003: return "pure virtual call";
    case 0xE0000004: return "invalid CRT parameter";
    default: return "unhandled exception";
  }
}

// The report: <stem>.dmp, <stem>.txt (what, where, the build) and <stem>-startup.log; then the marker naming the .txt
static void writeReport(EXCEPTION_POINTERS* ep) {
  // one report: a fault while writing it ends here; another thread's fault waits while this one finishes, and the
  // process ends when it has
  const LONG self = (LONG)GetCurrentThreadId(), writer = InterlockedCompareExchange(&s_writer, self, 0);
  if (writer == self) return;
  if (writer != 0) Sleep(INFINITE);
  CreateDirectoryA(s_dir, nullptr);
  SYSTEMTIME t; GetLocalTime(&t);
  char stem[MAX_PATH], path[MAX_PATH];
  snprintf(stem, sizeof stem, "%s\\crash-%04u%02u%02u-%02u%02u%02u", s_dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
  bool dumped = false;
  if (s_writeDump) {
    snprintf(path, sizeof path, "%s.dmp", stem);
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
      MINIDUMP_EXCEPTION_INFORMATION mi = {GetCurrentThreadId(), ep, FALSE};
      // the threads' stacks and what they point at: enough for a stack trace and the locals, no whole heap
      const MINIDUMP_TYPE type = (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory);
      dumped = s_writeDump(GetCurrentProcess(), GetCurrentProcessId(), f, type, ep ? &mi : nullptr, nullptr, nullptr);
      CloseHandle(f);
      if (!dumped) DeleteFileA(path);
    }
  }
  snprintf(path, sizeof path, "%s-startup.log", stem);
  CopyFileA(s_log, path, FALSE);
  snprintf(path, sizeof path, "%s.txt", stem);
  if (FILE* f = fopen(path, "w")) {
    const DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    void* at = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    fprintf(f, "Somnium Volandi crash report\nBuild: %s\nTime: %04u-%02u-%02u %02u:%02u:%02u\n", s_version, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    fprintf(f, "What: %s (0x%08lX)\n", codeName(code), (unsigned long)code);
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
      const ULONG_PTR* p = ep->ExceptionRecord->ExceptionInformation;
      fprintf(f, "Access: %s address 0x%llX\n", p[0] == 0 ? "read of" : p[0] == 1 ? "write to" : "execute at", (unsigned long long)p[1]);
    }
    HMODULE m = nullptr; char mod[MAX_PATH] = "?";
    if (at && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)at, &m))
      GetModuleFileNameA(m, mod, MAX_PATH);
    fprintf(f, "Where: %s + 0x%llX\n", mod, (unsigned long long)((char*)at - (char*)m));
    fprintf(f, "Thread: %lu\nDump: %s\n", (unsigned long)GetCurrentThreadId(), dumped ? "written" : "not written");
    fclose(f);
    char marker[MAX_PATH];
    snprintf(marker, sizeof marker, "%s\\last_crash.txt", s_dir);
    if (FILE* mf = fopen(marker, "w")) { fprintf(mf, "%s\n", path); fclose(mf); }
  }
}

static LONG WINAPI onUnhandled(EXCEPTION_POINTERS* ep) {
  writeReport(ep);
  return EXCEPTION_EXECUTE_HANDLER;   // (the process ends quietly; the next launch tells the player)
}

// A failure the CRT reports rather than raises: a report from here, with this thread's context, then the process ends
static void reportHere(DWORD code) {
  CONTEXT c = {}; RtlCaptureContext(&c);
  EXCEPTION_RECORD r = {}; r.ExceptionCode = code; r.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
#if defined(_M_X64) || defined(__x86_64__)
  r.ExceptionAddress = (void*)c.Rip;
#endif
  EXCEPTION_POINTERS ep = {&r, &c};
  writeReport(&ep);
  TerminateProcess(GetCurrentProcess(), 3);
}
static void onTerminate() { reportHere(0xE0000001); }
static void onAbort(int) { reportHere(0xE0000002); }
static void onPureCall() { reportHere(0xE0000003); }
static void onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) { reportHere(0xE0000004); }

void crashInstall(const std::string& crashDir, const std::string& logPath, const char* version) {
  if (!s_writeDump) {
    if (HMODULE h = LoadLibraryA("dbghelp.dll")) s_writeDump = (MiniDumpWriteDumpFn)(void*)GetProcAddress(h, "MiniDumpWriteDump");
    snprintf(s_dir, sizeof s_dir, "%s", crashDir.c_str());
    snprintf(s_log, sizeof s_log, "%s", logPath.c_str());
    snprintf(s_version, sizeof s_version, "%s", version && *version ? version : "unknown");
    ULONG guarantee = 64 * 1024; SetThreadStackGuarantee(&guarantee);   // (a stack overflow still has room to write it)
    std::set_terminate(onTerminate);
    signal(SIGABRT, onAbort);
    _set_purecall_handler(onPureCall);
    _set_invalid_parameter_handler(onInvalidParameter);
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);   // (abort() reaches onAbort, not Windows Error Reporting)
#endif
  }
  SetUnhandledExceptionFilter(onUnhandled);
}
