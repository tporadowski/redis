/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/*
 * Native Windows crash stacks via SEH + DbgHelp.
 * No Win32_RedisLog, no PORT_LONG, no C++ try/catch around the walk.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <Windows.h>
#include <DbgHelp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Win32_StackTrace.h"

#ifndef LL_WARNING
#define LL_WARNING 3
#endif
#ifndef LL_RAW
#define LL_RAW (1 << 10)
#endif

#define WIN32_STACK_MAX_FRAMES 64

extern "C" {
void _serverLog(int level, const char *fmt, ...);
void serverLogRaw(int level, const char *msg);
int bugReportStart(void);
void logServerInfo(void);
}

static volatile LONG g_processing;
static volatile LONG g_installed;
static LPTOP_LEVEL_EXCEPTION_FILTER g_prev_filter;
static void (__cdecl *g_prev_abort)(int);

static const char *exception_name(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:         return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT:               return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:    return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DENORMAL_OPERAND:     return "EXCEPTION_FLT_DENORMAL_OPERAND";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INEXACT_RESULT:       return "EXCEPTION_FLT_INEXACT_RESULT";
    case EXCEPTION_FLT_INVALID_OPERATION:    return "EXCEPTION_FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW:             return "EXCEPTION_FLT_OVERFLOW";
    case EXCEPTION_FLT_STACK_CHECK:          return "EXCEPTION_FLT_STACK_CHECK";
    case EXCEPTION_FLT_UNDERFLOW:            return "EXCEPTION_FLT_UNDERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:      return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:            return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:             return "EXCEPTION_INT_OVERFLOW";
    case EXCEPTION_INVALID_DISPOSITION:      return "EXCEPTION_INVALID_DISPOSITION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_PRIV_INSTRUCTION:         return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_SINGLE_STEP:              return "EXCEPTION_SINGLE_STEP";
    case EXCEPTION_STACK_OVERFLOW:           return "EXCEPTION_STACK_OVERFLOW";
    default:                                 return "UNKNOWN EXCEPTION";
    }
}

static const char *filename_start(const char *path) {
    const char *slash;
    const char *backslash;
    if (path == NULL || path[0] == '\0') return "(unknown)";
    backslash = strrchr(path, '\\');
    slash = strrchr(path, '/');
    if (slash != NULL && (backslash == NULL || slash > backslash))
        backslash = slash;
    return backslash == NULL ? path : backslash + 1;
}

static void module_name_for_pc(DWORD64 pc, char *out, size_t out_len) {
    HMODULE module = NULL;
    char path[MAX_PATH];

    out[0] = '\0';
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(uintptr_t)pc, &module) ||
        module == NULL) {
        return;
    }
    if (GetModuleFileNameA(module, path, MAX_PATH) == 0)
        return;
    snprintf(out, out_len, "%s", filename_start(path));
}

void win32_log_stack_trace(const void *context) {
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    CONTEXT ctx;
    STACKFRAME64 frame;
    unsigned char symbol_storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
    SYMBOL_INFO *symbol = (SYMBOL_INFO *)symbol_storage;
    IMAGEHLP_LINE64 line;
    DWORD64 displacement64;
    DWORD displacement;
    int n;

    if (context != NULL) {
        ctx = *(const CONTEXT *)context;
    } else {
        RtlCaptureContext(&ctx);
    }

    memset(&frame, 0, sizeof(frame));
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;

    _serverLog(LL_WARNING, "--- STACK TRACE");
    for (n = 0; n < WIN32_STACK_MAX_FRAMES; n++) {
        char module[MAX_PATH];
        const char *name;
        const char *source;
        unsigned int lineno;

        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame,
                         &ctx, NULL, SymFunctionTableAccess64,
                         SymGetModuleBase64, NULL))
            break;
        if (frame.AddrPC.Offset == 0)
            break;

        memset(symbol_storage, 0, sizeof(symbol_storage));
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;
        displacement64 = 0;
        name = "(no symbol)";
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement64, symbol) &&
            symbol->Name[0] != '\0')
            name = symbol->Name;

        memset(&line, 0, sizeof(line));
        line.SizeOfStruct = sizeof(line);
        displacement = 0;
        source = NULL;
        lineno = 0;
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &displacement,
                                 &line) &&
            line.FileName != NULL) {
            source = filename_start(line.FileName);
            lineno = (unsigned int)line.LineNumber;
        }

        module_name_for_pc(frame.AddrPC.Offset, module, sizeof(module));
        if (source != NULL) {
            _serverLog(LL_WARNING,
                       "%s!%s+0x%llx (%s:%u) pc=0x%llx",
                       module[0] ? module : "(unknown)",
                       name,
                       (unsigned long long)displacement64,
                       source,
                       lineno,
                       (unsigned long long)(uintptr_t)frame.AddrPC.Offset);
        } else {
            _serverLog(LL_WARNING,
                       "%s!%s+0x%llx pc=0x%llx",
                       module[0] ? module : "(unknown)",
                       name,
                       (unsigned long long)displacement64,
                       (unsigned long long)(uintptr_t)frame.AddrPC.Offset);
        }
    }
}

static void log_bug_report_end(void) {
    serverLogRaw(LL_WARNING | LL_RAW,
"\n=== REDIS BUG REPORT END. Make sure to include from START to END. ===\n\n"
"       Please report the crash by opening an issue on github:\n\n"
"           https://github.com/tporadowski/redis/issues\n\n"
"  If a Redis module was involved, please open in the module's repo instead.\n\n"
"  Suspect RAM error? Use redis-server --test-memory to verify it.\n\n"
"  Some other issues could be detected by redis-server --check-system\n");
}

static LONG WINAPI unhandled_exception(EXCEPTION_POINTERS *info) {
    if (InterlockedCompareExchange(&g_processing, 1, 0) != 0)
        return EXCEPTION_CONTINUE_SEARCH;

    bugReportStart();
    if (info != NULL && info->ExceptionRecord != NULL) {
        uintptr_t fault = 0;
        if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
            info->ExceptionRecord->NumberParameters >= 2) {
            fault = (uintptr_t)info->ExceptionRecord->ExceptionInformation[1];
        }
        _serverLog(LL_WARNING, "--- %s",
                   exception_name(info->ExceptionRecord->ExceptionCode));
        _serverLog(LL_WARNING,
                   "--- exception context: code=0x%08x address=0x%llx fault=0x%llx",
                   (unsigned int)info->ExceptionRecord->ExceptionCode,
                   (unsigned long long)(uintptr_t)
                       info->ExceptionRecord->ExceptionAddress,
                   (unsigned long long)fault);
        if (info->ContextRecord != NULL)
            win32_log_stack_trace(info->ContextRecord);
        else
            win32_log_stack_trace(NULL);
    } else {
        _serverLog(LL_WARNING, "--- UNKNOWN EXCEPTION");
        win32_log_stack_trace(NULL);
    }
    logServerInfo();
    log_bug_report_end();

    InterlockedExchange(&g_processing, 0);
    if (g_prev_filter != NULL && info != NULL)
        return g_prev_filter(info);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void abort_handler(int signo) {
    (void)signo;
    if (InterlockedCompareExchange(&g_processing, 1, 0) != 0)
        return;
    bugReportStart();
    _serverLog(LL_WARNING, "--- ABORT");
    win32_log_stack_trace(NULL);
    log_bug_report_end();
    InterlockedExchange(&g_processing, 0);
}

void StackTraceInit(void) {
    HANDLE process = GetCurrentProcess();

    if (InterlockedCompareExchange(&g_installed, 1, 0) != 0)
        return;

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, NULL, TRUE);
    g_prev_filter = SetUnhandledExceptionFilter(unhandled_exception);
    g_prev_abort = signal(SIGABRT, abort_handler);
}

void StackTraceShutdown(void) {
    if (InterlockedCompareExchange(&g_installed, 0, 1) != 1)
        return;
    SetUnhandledExceptionFilter(g_prev_filter);
    g_prev_filter = NULL;
    signal(SIGABRT, g_prev_abort ? g_prev_abort : SIG_DFL);
    g_prev_abort = NULL;
}
