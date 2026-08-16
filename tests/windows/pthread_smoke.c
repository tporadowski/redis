/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/* pthread_join + cond broadcast + thread-control accounting. */
#include "Win32_Interop/Win32_PThread.h"
#include "Win32_Interop/Win32_ThreadControl.h"
#include <stdio.h>
#include <stdlib.h>

#define NWAIT 8

static pthread_mutex_t mu;
static pthread_cond_t cv;
static int go;
static int woke;

static void *waiter(void *arg) {
    (void)arg;
    pthread_mutex_lock(&mu);
    while (!go)
        pthread_cond_wait(&cv, &mu);
    woke++;
    pthread_mutex_unlock(&mu);
    return NULL;
}

static void *just_return(void *arg) {
    return arg;
}

int main(void) {
    pthread_t tids[NWAIT];
    pthread_t t;
    void *ret = NULL;
    int i;

    InitThreadControl();
    pthread_mutex_init(&mu, NULL);
    pthread_cond_init(&cv, NULL);

    if (pthread_create(&t, NULL, just_return, (void *)(intptr_t)42) != 0) {
        fprintf(stderr, "pthread_create failed\n");
        return 1;
    }
    if (pthread_join(t, &ret) != 0) {
        fprintf(stderr, "pthread_join failed\n");
        return 1;
    }

    go = 0;
    woke = 0;
    for (i = 0; i < NWAIT; i++) {
        if (pthread_create(&tids[i], NULL, waiter, NULL) != 0) {
            fprintf(stderr, "waiter create failed\n");
            return 1;
        }
    }
    /* Let waiters reach cond_wait. */
    Sleep(100);
    pthread_mutex_lock(&mu);
    go = 1;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
    for (i = 0; i < NWAIT; i++) {
        if (pthread_join(tids[i], NULL) != 0) {
            fprintf(stderr, "waiter join failed\n");
            return 1;
        }
    }
    if (woke != NWAIT) {
        fprintf(stderr, "broadcast woke %d/%d\n", woke, NWAIT);
        return 1;
    }

    RequestSuspension();
    if (!SuspensionCompleted()) {
        /* No workers should be running now. */
        fprintf(stderr, "expected no live workers after joins\n");
        return 1;
    }
    ResumeFromSuspension();

    if (pthread_cancel(t) == 0) {
        fprintf(stderr, "pthread_cancel must not TerminateThread\n");
        return 1;
    }

    pthread_cond_destroy(&cv);
    pthread_mutex_destroy(&mu);
    printf("pthread_smoke: ok (join, broadcast %d, no TerminateThread)\n", NWAIT);
    return 0;
}
