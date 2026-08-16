/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/*
 * Win32 pthread subset for the Redis 8.10 Windows port.
 * Join keeps the _beginthreadex HANDLE. Broadcast wakes every waiter.
 * pthread_cancel does not TerminateThread (Decision 11).
 */

#include "Win32_PThread.h"
#include "Win32_ThreadControl.h"
#include <process.h>
#include <stdlib.h>

#define REDIS_THREAD_STACK_SIZE (1024 * 1024 * 4)
#ifndef STACK_SIZE_PARAM_IS_A_RESERVATION
#define STACK_SIZE_PARAM_IS_A_RESERVATION 0x00010000
#endif

#ifndef UNUSED
#define UNUSED(V) ((void)(V))
#endif

#define WIN32_PTHREAD_MAX 512

typedef struct {
    void *(*func)(void *);
    void *arg;
} thread_params;

static CRITICAL_SECTION g_handle_lock;
static int g_handle_lock_ready;
static struct {
    pthread_t id;
    HANDLE handle;
    int used;
} g_threads[WIN32_PTHREAD_MAX];

static void ensure_handle_lock(void) {
    if (!g_handle_lock_ready) {
        InitializeCriticalSection(&g_handle_lock);
        g_handle_lock_ready = 1;
    }
}

static void register_thread(pthread_t id, HANDLE handle) {
    int i;
    ensure_handle_lock();
    EnterCriticalSection(&g_handle_lock);
    for (i = 0; i < WIN32_PTHREAD_MAX; i++) {
        if (!g_threads[i].used) {
            g_threads[i].id = id;
            g_threads[i].handle = handle;
            g_threads[i].used = 1;
            break;
        }
    }
    LeaveCriticalSection(&g_handle_lock);
}

static HANDLE take_thread_handle(pthread_t id) {
    int i;
    HANDLE h = NULL;
    ensure_handle_lock();
    EnterCriticalSection(&g_handle_lock);
    for (i = 0; i < WIN32_PTHREAD_MAX; i++) {
        if (g_threads[i].used && g_threads[i].id == id) {
            h = g_threads[i].handle;
            g_threads[i].used = 0;
            g_threads[i].handle = NULL;
            break;
        }
    }
    LeaveCriticalSection(&g_handle_lock);
    return h;
}

static unsigned __stdcall win32_proxy_threadproc(void *arg) {
    thread_params *p = (thread_params *)arg;
    IncrementWorkerThreadCount();
    p->func(p->arg);
    free(p);
    DecrementWorkerThreadCount();
    _endthreadex(0);
    return 0;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) {
    HANDLE h;
    unsigned tid = 0;
    size_t stack = REDIS_THREAD_STACK_SIZE;
    thread_params *params = (thread_params *)malloc(sizeof(*params));
    if (!params) return ENOMEM;
    if (attr && *attr) stack = *attr;
    params->func = start_routine;
    params->arg = arg;
    h = (HANDLE)_beginthreadex(NULL, (unsigned)stack, win32_proxy_threadproc,
                               params, STACK_SIZE_PARAM_IS_A_RESERVATION, &tid);
    if (!h) {
        free(params);
        return errno ? errno : EAGAIN;
    }
    *thread = tid;
    register_thread(tid, h);
    return 0;
}

int pthread_join(pthread_t thread, void **value_ptr) {
    HANDLE h = take_thread_handle(thread);
    DWORD rc;
    UNUSED(value_ptr);
    if (!h) {
        h = OpenThread(SYNCHRONIZE, FALSE, thread);
        if (!h) return ESRCH;
    }
    rc = WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
    return rc == WAIT_OBJECT_0 ? 0 : EINVAL;
}

int pthread_detach(pthread_t thread) {
    HANDLE h = take_thread_handle(thread);
    if (h) CloseHandle(h);
    return 0;
}

pthread_t pthread_self(void) {
    return GetCurrentThreadId();
}

int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset) {
    UNUSED(set);
    UNUSED(oldset);
    if (how != SIG_BLOCK && how != SIG_UNBLOCK && how != SIG_SETMASK) {
        errno = EINVAL;
        return -1;
    }
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
