/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_SYS_RESOURCE_H
#define WIN32_POSIX_SYS_RESOURCE_H
#include <string.h>
#include "time.h"
#define RLIMIT_NOFILE 7
#define RUSAGE_SELF 0
#define RUSAGE_CHILDREN -1
#define RUSAGE_THREAD 1
typedef unsigned long rlim_t;
struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};
struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
};
static inline int getrlimit(int res, struct rlimit *rl) {
    (void)res;
    if (rl) { rl->rlim_cur = 8192; rl->rlim_max = 8192; }
    return 0;
}
static inline int setrlimit(int res, const struct rlimit *rl) {
    (void)res; (void)rl;
    return 0;
}
static inline int getrusage(int who, struct rusage *ru) {
    (void)who;
    if (ru) memset(ru, 0, sizeof(*ru));
    return 0;
}
#endif
