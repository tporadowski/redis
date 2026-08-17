/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <errno.h>
#include "Win32_ProcessTable.h"
#include "win32_types.h"
#include "Win32_Signal.h"

#ifndef WNOHANG
#define WNOHANG 1
#endif

#define WP_MAX 16

typedef struct {
    pid_t pid;
    HANDLE process;
    WinPidKind kind;
    HANDLE abort_event;
    HANDLE retain;
    int used;
} WinPidEntry;

static WinPidEntry g_pids[WP_MAX];
static CRITICAL_SECTION g_lock;
static volatile LONG g_ready;

static void wp_init(void) {
    if (InterlockedCompareExchange(&g_ready, 1, 0) != 0)
        return;
    InitializeCriticalSection(&g_lock);
}

void winpid_register(pid_t pid, HANDLE process, WinPidKind kind, HANDLE abort_event) {
    winpid_register_retain(pid, process, kind, abort_event, NULL);
}

void winpid_register_retain(pid_t pid, HANDLE process, WinPidKind kind,
                            HANDLE abort_event, HANDLE retain) {
    wp_init();
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < WP_MAX; i++) {
        if (!g_pids[i].used) {
            g_pids[i].pid = pid;
            g_pids[i].process = process;
            g_pids[i].kind = kind;
            g_pids[i].abort_event = abort_event;
            g_pids[i].retain = retain;
            g_pids[i].used = 1;
            LeaveCriticalSection(&g_lock);
            return;
        }
    }
    LeaveCriticalSection(&g_lock);
    if (retain)
        CloseHandle(retain);
}

void winpid_unregister(pid_t pid) {
    wp_init();
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < WP_MAX; i++) {
        if (g_pids[i].used && g_pids[i].pid == pid) {
            if (g_pids[i].retain) {
                CloseHandle(g_pids[i].retain);
                g_pids[i].retain = NULL;
            }
            g_pids[i].used = 0;
            g_pids[i].process = NULL;
            g_pids[i].abort_event = NULL;
            break;
        }
    }
    LeaveCriticalSection(&g_lock);
}

static int status_from_exit(DWORD code) {
    /* 128+sig from TerminateProcess(kill) → WIFSIGNALED. Else WIFEXITED. */
    if (code >= 128 && code < 256)
        return (int)((code - 128) & 0x7f);
    return (int)((code & 0xff) << 8);
}

pid_t waitpid(pid_t pid, int *status, int options) {
    wp_init();
    DWORD timeout = (options & WNOHANG) ? 0 : INFINITE;

    EnterCriticalSection(&g_lock);
    WinPidEntry *e = NULL;
    if (pid == -1) {
        /* Only Sentinel scripts. Never reap a QFork child via waitpid(-1). */
        for (int i = 0; i < WP_MAX; i++) {
            if (g_pids[i].used && g_pids[i].kind == WP_SENTINEL_SCRIPT) {
                DWORD wr = WaitForSingleObject(g_pids[i].process, 0);
                if (wr == WAIT_OBJECT_0) {
                    e = &g_pids[i];
                    break;
                }
            }
        }
        if (!e) {
            LeaveCriticalSection(&g_lock);
            return 0;
        }
    } else {
        for (int i = 0; i < WP_MAX; i++) {
            if (g_pids[i].used && g_pids[i].pid == pid) {
                e = &g_pids[i];
                break;
            }
        }
        if (!e) {
            LeaveCriticalSection(&g_lock);
            errno = ECHILD;
            return -1;
        }
    }

    HANDLE proc = e->process;
    pid_t out = e->pid;
    LeaveCriticalSection(&g_lock);

    DWORD wr = WaitForSingleObject(proc, timeout);
    if (wr == WAIT_TIMEOUT)
        return 0;
    if (wr != WAIT_OBJECT_0) {
        errno = EINVAL;
        return -1;
    }

    DWORD code = 0;
    GetExitCodeProcess(proc, &code);
    if (status)
        *status = status_from_exit(code);
    CloseHandle(proc);
    winpid_unregister(out);
    return out;
}

int kill(pid_t pid, int sig) {
    wp_init();
    EnterCriticalSection(&g_lock);
    WinPidEntry *e = NULL;
    for (int i = 0; i < WP_MAX; i++) {
        if (g_pids[i].used && g_pids[i].pid == pid) {
            e = &g_pids[i];
            break;
        }
    }
    if (!e) {
        LeaveCriticalSection(&g_lock);
        errno = ESRCH;
        return -1;
    }
    HANDLE proc = e->process;
    HANDLE abort_ev = e->abort_event;
    WinPidKind kind = e->kind;
    LeaveCriticalSection(&g_lock);

    if (kind == WP_QFORK && sig == SIGUSR1 && abort_ev) {
        SetEvent(abort_ev);
        return 0;
    }
    if (!TerminateProcess(proc, (DWORD)(128 + (sig ? sig : 1)))) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}
