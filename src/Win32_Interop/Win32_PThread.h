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
#define pthread_mutex_destroy(a) DeleteCriticalSection((a))
#define pthread_mutex_lock EnterCriticalSection
#define pthread_mutex_unlock LeaveCriticalSection
#define pthread_equal(t1, t2) ((t1) == (t2))

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

#ifdef __cplusplus
}
#endif

#endif
