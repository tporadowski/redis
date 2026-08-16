/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_SYS_TIME_H
#define WIN32_POSIX_SYS_TIME_H
#include "../../Win32_Time.h"
#ifndef ITIMER_REAL
#define ITIMER_REAL 0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF 2
#endif
struct itimerval {
    struct timeval it_interval;
    struct timeval it_value;
};
#ifdef __cplusplus
extern "C" {
#endif
int setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value);
#ifdef __cplusplus
}
#endif
#endif
