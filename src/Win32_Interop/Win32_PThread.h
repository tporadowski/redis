/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/*
 * Lifted from MSOpenTech / tporadowski Redis 5.0.14 Win32_PThread.h
 * and extended with pthread_join + cond broadcast (plan 0.2 / Decision 11).
 */
#ifndef WIN32_INTEROP_PTHREAD_H
#define WIN32_INTEROP_PTHREAD_H

#include <windows.h>
#include <errno.h>
#include "win32_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _SIGSET_T_
#define _SIGSET_T_
typedef size_t _sigset_t;
#define sigset_t _sigset_t
#endif

#ifndef SIG_SETMASK
#define SIG_SETMASK (0)
#define SIG_BLOCK   (1)
#define SIG_UNBLOCK (2)
#endif

#define pthread_mutex_t CRITICAL_SECTION
#define pthread_attr_t size_t
#define pthread_t unsigned int

#define pthread_mutex_init(a,b) (InitializeCriticalSectionAndSpinCount((a), 0x80000400),0)
#define pthread_mutex_destroy(a) (DeleteCriticalSection((a)),0)
#define pthread_equal(t1, t2) ((t1) == (t2))
#define PTHREAD_MUTEX_INITIALIZER {0}
static __inline void win32_mutex_ensure(pthread_mutex_t *m) {
    if (m->DebugInfo == NULL)
        InitializeCriticalSectionAndSpinCount(m, 0x80000400);
}
static __inline int pthread_mutex_lock(pthread_mutex_t *m) {
    win32_mutex_ensure(m);
    EnterCriticalSection(m);
    return 0;
}
static __inline int pthread_mutex_unlock(pthread_mutex_t *m) {
    LeaveCriticalSection(m);
    return 0;
}
static __inline int pthread_mutex_trylock(pthread_mutex_t *m) {
    win32_mutex_ensure(m);
    return TryEnterCriticalSection(m) ? 0 : EBUSY;
}

typedef int pthread_mutexattr_t;
#ifndef PTHREAD_MUTEX_NORMAL
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_RECURSIVE 1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_ADAPTIVE_NP 3
#endif
static __inline int pthread_mutexattr_init(pthread_mutexattr_t *a) {
    if (a) *a = 0;
    return 0;
}
static __inline int pthread_mutexattr_settype(pthread_mutexattr_t *a, int t) {
    (void)a;
    (void)t;
    return 0;
}
static __inline int pthread_mutexattr_destroy(pthread_mutexattr_t *a) {
    (void)a;
    return 0;
}

#define pthread_attr_init(x) (*(x) = 0, 0)
#define pthread_attr_destroy(x) (0)
#define pthread_attr_getstacksize(x, y) (*(y) = *(x), 0)
#define pthread_attr_setstacksize(x, y) (*(x) = (y), 0)

typedef struct {
    CRITICAL_SECTION waiters_lock;
    LONG waiters;
    int was_broadcast;
    HANDLE sema;
    HANDLE continue_broadcast;
} pthread_cond_t;

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg);
int pthread_join(pthread_t thread, void **value_ptr);
int pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset);
int pthread_cancel(pthread_t thread);

int pthread_cond_init(pthread_cond_t *cond, const void *unused);
int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_broadcast(pthread_cond_t *cond);

/* SRWLOCK cannot Release without knowing shared vs exclusive. Track writer. */
typedef struct {
    SRWLOCK lock;
    volatile LONG exclusive_tid;
} pthread_rwlock_t;
typedef int pthread_rwlockattr_t;

static __inline int pthread_rwlock_init(pthread_rwlock_t *rw,
                                        const pthread_rwlockattr_t *attr) {
    (void)attr;
    InitializeSRWLock(&rw->lock);
    rw->exclusive_tid = 0;
    return 0;
}
static __inline int pthread_rwlock_destroy(pthread_rwlock_t *rw) {
    (void)rw;
    return 0;
}
static __inline int pthread_rwlock_rdlock(pthread_rwlock_t *rw) {
    AcquireSRWLockShared(&rw->lock);
    return 0;
}
static __inline int pthread_rwlock_wrlock(pthread_rwlock_t *rw) {
    AcquireSRWLockExclusive(&rw->lock);
    rw->exclusive_tid = (LONG)GetCurrentThreadId();
    return 0;
}
static __inline int pthread_rwlock_unlock(pthread_rwlock_t *rw) {
    if ((DWORD)rw->exclusive_tid == GetCurrentThreadId()) {
        rw->exclusive_tid = 0;
        ReleaseSRWLockExclusive(&rw->lock);
    } else {
        ReleaseSRWLockShared(&rw->lock);
    }
    return 0;
}

#ifndef PTHREAD_CANCEL_ENABLE
#define PTHREAD_CANCEL_ENABLE 0
#define PTHREAD_CANCEL_DISABLE 1
#define PTHREAD_CANCEL_DEFERRED 0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1
#endif
int pthread_setcancelstate(int state, int *oldstate);
int pthread_setcanceltype(int type, int *oldtype);

#ifdef __cplusplus
}
#endif

#endif
