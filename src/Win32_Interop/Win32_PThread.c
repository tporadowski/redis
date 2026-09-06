/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/*
 * Win32 pthread subset for the Redis 8.10 Windows port.
 * Join keeps the _beginthreadex HANDLE. Broadcast wakes every waiter.
 * pthread_cancel does not TerminateThread (Decision 11).
 */

#include "Win32_PThread.h"
#include "Win32_Error.h"
#include <limits.h>
#include <process.h>
#include <stdint.h>
#include <stdlib.h>

#define REDIS_THREAD_STACK_SIZE (1024 * 1024 * 4)
#ifndef STACK_SIZE_PARAM_IS_A_RESERVATION
#define STACK_SIZE_PARAM_IS_A_RESERVATION 0x00010000
#endif
#ifndef CREATE_SUSPENDED
#define CREATE_SUSPENDED 0x00000004
#endif

#ifndef UNUSED
#define UNUSED(V) ((void)(V))
#endif

typedef struct win32_thread_record {
    pthread_t id;
    HANDLE handle;
    void *result;
    int completed;
    int detached;
    int joined;
    struct win32_thread_record *next;
} win32_thread_record;

typedef struct {
    void *(*func)(void *);
    void *arg;
    win32_thread_record *record;
} thread_params;

static INIT_ONCE thread_registry_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION thread_registry_lock;
static win32_thread_record *thread_registry;
static __declspec(thread) pthread_t current_thread_id;

static BOOL CALLBACK init_thread_registry(PINIT_ONCE once, PVOID parameter,
                                          PVOID *context) {
    UNUSED(once);
    UNUSED(parameter);
    UNUSED(context);
    InitializeCriticalSectionAndSpinCount(&thread_registry_lock, 0x80000400);
    return TRUE;
}

static void ensure_thread_registry(void) {
    InitOnceExecuteOnce(&thread_registry_once, init_thread_registry, NULL, NULL);
}

static win32_thread_record *find_thread_record(pthread_t id) {
    win32_thread_record *record;
    for (record = thread_registry; record != NULL; record = record->next) {
        if (record->id == id) return record;
    }
    return NULL;
}

static void remove_thread_record(win32_thread_record *record) {
    win32_thread_record **current = &thread_registry;
    while (*current != NULL) {
        if (*current == record) {
            *current = record->next;
            return;
        }
        current = &(*current)->next;
    }
}

/* BIO threads register with ThreadControl themselves. IO threads use
 * pauseAllIOThreads and must not inflate the BIO worker count. */
static unsigned __stdcall win32_proxy_threadproc(void *arg) {
    thread_params *p = (thread_params *)arg;
    win32_thread_record *record = p->record;
    void *result;
    int release_record = 0;

    current_thread_id = record->id;
    result = p->func(p->arg);
    free(p);

    ensure_thread_registry();
    EnterCriticalSection(&thread_registry_lock);
    record->result = result;
    record->completed = 1;
    if (record->detached) {
        remove_thread_record(record);
        release_record = 1;
    }
    LeaveCriticalSection(&thread_registry_lock);

    if (release_record) {
        CloseHandle(record->handle);
        free(record);
    }

    _endthreadex(0);
    return 0;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) {
    HANDLE handle;
    DWORD resume_result;
    size_t stack = REDIS_THREAD_STACK_SIZE;
    thread_params *params;
    win32_thread_record *record;

    if (thread == NULL || start_routine == NULL) return EINVAL;
    if (attr != NULL && *attr != 0) {
        if (*attr > (size_t)UINT_MAX) return EINVAL;
        stack = *attr;
    }

    params = (thread_params *)malloc(sizeof(*params));
    record = (win32_thread_record *)calloc(1, sizeof(*record));
    if (params == NULL || record == NULL) {
        free(params);
        free(record);
        return ENOMEM;
    }
    params->func = start_routine;
    params->arg = arg;
    params->record = record;

    handle = (HANDLE)_beginthreadex(
        NULL, (unsigned)stack, win32_proxy_threadproc, params,
        STACK_SIZE_PARAM_IS_A_RESERVATION | CREATE_SUSPENDED, NULL);
    if (handle == NULL) {
        int error = errno != 0 ? errno : EAGAIN;
        free(params);
        free(record);
        return error;
    }

    /* Windows may recycle a numeric thread ID as soon as a thread exits,
     * even while a joinable handle is still retained. Use the live record's
     * address as the opaque pthread_t so an unjoined thread cannot collide
     * with a later thread. */
    record->id = (pthread_t)(uintptr_t)record;
    record->handle = handle;
    ensure_thread_registry();
    EnterCriticalSection(&thread_registry_lock);
    record->next = thread_registry;
    thread_registry = record;
    LeaveCriticalSection(&thread_registry_lock);

    resume_result = ResumeThread(handle);
    if (resume_result == (DWORD)-1) {
        DWORD error = GetLastError();
        EnterCriticalSection(&thread_registry_lock);
        remove_thread_record(record);
        LeaveCriticalSection(&thread_registry_lock);
        TerminateThread(handle, ERROR_OPERATION_ABORTED);
        CloseHandle(handle);
        free(params);
        free(record);
        return win32_errno_from_system_error((int)error);
    }

    *thread = record->id;
    return 0;
}

int pthread_join(pthread_t thread, void **value_ptr) {
    win32_thread_record *record;
    DWORD wait_result;

    if (thread == pthread_self()) return EDEADLK;
    ensure_thread_registry();
    EnterCriticalSection(&thread_registry_lock);
    record = find_thread_record(thread);
    if (record == NULL) {
        LeaveCriticalSection(&thread_registry_lock);
        return ESRCH;
    }
    if (record->detached || record->joined) {
        LeaveCriticalSection(&thread_registry_lock);
        return EINVAL;
    }
    record->joined = 1;
    LeaveCriticalSection(&thread_registry_lock);

    wait_result = WaitForSingleObject(record->handle, INFINITE);
    if (wait_result != WAIT_OBJECT_0) {
        int result = wait_result == WAIT_FAILED ?
                     win32_errno_from_system_error((int)GetLastError()) :
                     EINVAL;
        EnterCriticalSection(&thread_registry_lock);
        record->joined = 0;
        LeaveCriticalSection(&thread_registry_lock);
        return result;
    }

    EnterCriticalSection(&thread_registry_lock);
    if (value_ptr != NULL) *value_ptr = record->result;
    remove_thread_record(record);
    LeaveCriticalSection(&thread_registry_lock);
    CloseHandle(record->handle);
    free(record);
    return 0;
}

int pthread_detach(pthread_t thread) {
    win32_thread_record *record;
    int release_record = 0;

    ensure_thread_registry();
    EnterCriticalSection(&thread_registry_lock);
    record = find_thread_record(thread);
    if (record == NULL) {
        LeaveCriticalSection(&thread_registry_lock);
        return ESRCH;
    }
    if (record->detached || record->joined) {
        LeaveCriticalSection(&thread_registry_lock);
        return EINVAL;
    }
    record->detached = 1;
    if (record->completed) {
        remove_thread_record(record);
        release_record = 1;
    }
    LeaveCriticalSection(&thread_registry_lock);

    if (release_record) {
        CloseHandle(record->handle);
        free(record);
    }
    return 0;
}

pthread_t pthread_self(void) {
    if (current_thread_id != 0) return current_thread_id;
    /* Threads not created through pthread_create (the process main thread and
     * the SCM worker) still need a stable opaque identity. Keep that namespace
     * separate from aligned record pointers. */
    return ((pthread_t)GetCurrentThreadId() << 1) | 1;
}

int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset) {
    UNUSED(set);
    UNUSED(oldset);
    switch (how) {
        case SIG_BLOCK:
        case SIG_UNBLOCK:
        case SIG_SETMASK:
            break;
        default:
            return EINVAL;
    }
    /* Windows has no process-compatible per-thread signal mask. Redis only
     * uses this to block signals in worker threads, so the portable contract
     * is a successful no-op without contaminating the caller's errno. */
    return 0;
}

int pthread_cancel(pthread_t thread) {
    UNUSED(thread);
    errno = ENOSYS;
    return -1;
}

int pthread_setcancelstate(int state, int *oldstate) {
    if (oldstate) *oldstate = PTHREAD_CANCEL_DISABLE;
    UNUSED(state);
    return 0;
}

int pthread_setcanceltype(int type, int *oldtype) {
    if (oldtype) *oldtype = PTHREAD_CANCEL_DEFERRED;
    UNUSED(type);
    return 0;
}

int pthread_cond_init(pthread_cond_t *cond, const void *unused) {
    UNUSED(unused);
    cond->waiters = 0;
    cond->was_broadcast = 0;
    InitializeCriticalSection(&cond->waiters_lock);
    cond->sema = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
    if (!cond->sema) return ENOMEM;
    cond->continue_broadcast = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!cond->continue_broadcast) {
        CloseHandle(cond->sema);
        return ENOMEM;
    }
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond) {
    CloseHandle(cond->sema);
    CloseHandle(cond->continue_broadcast);
    DeleteCriticalSection(&cond->waiters_lock);
    return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    int last_waiter;
    EnterCriticalSection(&cond->waiters_lock);
    cond->waiters++;
    LeaveCriticalSection(&cond->waiters_lock);
    LeaveCriticalSection(mutex);
    WaitForSingleObject(cond->sema, INFINITE);
    EnterCriticalSection(&cond->waiters_lock);
    cond->waiters--;
    last_waiter = cond->was_broadcast && cond->waiters == 0;
    LeaveCriticalSection(&cond->waiters_lock);
    if (last_waiter) SetEvent(cond->continue_broadcast);
    EnterCriticalSection(mutex);
    return 0;
}

int pthread_cond_signal(pthread_cond_t *cond) {
    int have_waiters;
    EnterCriticalSection(&cond->waiters_lock);
    have_waiters = cond->waiters > 0;
    LeaveCriticalSection(&cond->waiters_lock);
    if (have_waiters) ReleaseSemaphore(cond->sema, 1, NULL);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
    LONG n = 0;
    EnterCriticalSection(&cond->waiters_lock);
    if (cond->waiters > 0) {
        cond->was_broadcast = 1;
        n = cond->waiters;
    }
    LeaveCriticalSection(&cond->waiters_lock);
    if (n > 0) {
        ReleaseSemaphore(cond->sema, n, NULL);
        WaitForSingleObject(cond->continue_broadcast, INFINITE);
        EnterCriticalSection(&cond->waiters_lock);
        cond->was_broadcast = 0;
        LeaveCriticalSection(&cond->waiters_lock);
    }
    return 0;
}
