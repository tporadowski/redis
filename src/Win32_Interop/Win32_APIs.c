/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#include "win32_pre.h"
#include "Win32_Time.h"
#include "Win32_FDAPI.h"

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#ifndef UNUSED
#define UNUSED(V) ((void)(V))
#endif

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME  0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

typedef int clockid_t;

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    if (!tp) {
        errno = EFAULT;
        return -1;
    }
    if (clk_id == CLOCK_MONOTONIC) {
        uint64_t us = GetHighResRelativeTime(1000000.0);
        tp->tv_sec = (time_t)(us / 1000000ULL);
        tp->tv_nsec = (long)((us % 1000000ULL) * 1000ULL);
        return 0;
    }
    {
        struct timeval tv;
        gettimeofday_highres(&tv, NULL);
        tp->tv_sec = tv.tv_sec;
        tp->tv_nsec = tv.tv_usec * 1000L;
        return 0;
    }
}

unsigned int sleep(unsigned int seconds) {
    Sleep(seconds * 1000U);
    return 0;
}

int usleep(unsigned int usec) {
    if (usec == 0) {
        Sleep(0);
        return 0;
    }
    Sleep((usec + 999) / 1000);
    return 0;
}

int mkstemp(char *template) {
    if (!_mktemp(template)) return -1;
    return _open(template, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY, 0600);
}

long random(void) {
    return (long)rand();
}

void srandom(unsigned int seed) {
    srand(seed);
}

int geteuid(void) {
    return 0;
}

int gethostname(char *name, size_t len) {
    DWORD n = (DWORD)len;
    FDAPI_Init();
    if (!GetComputerNameA(name, &n)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}
