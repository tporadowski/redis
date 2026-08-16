/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_INTEROP_SIGNAL_H
#define WIN32_INTEROP_SIGNAL_H

#ifdef _WIN32

#include <signal.h>
#include "win32_types.h"

#ifndef _SIGSET_T_
#define _SIGSET_T_
typedef size_t _sigset_t;
#define sigset_t _sigset_t
#endif

#ifndef SA_RESTART
#define SA_RESTART  0x10000000
#endif
#ifndef SA_SIGINFO
#define SA_SIGINFO  4
#endif
#ifndef SA_NOCLDSTOP
#define SA_NOCLDSTOP 1
#endif

typedef struct {
    int si_signo;
    int si_code;
    int si_pid;
    int si_status;
    int si_uid;
    void *si_addr;
} siginfo_t;

#ifndef SI_USER
#define SI_USER 0
#endif
#ifndef SA_NODEFER
#define SA_NODEFER 0x40000000
#endif
#ifndef SA_RESETHAND
#define SA_RESETHAND 0x80000000
#endif

struct sigaction {
    void (*sa_handler)(int);
    void (*sa_sigaction)(int, siginfo_t *, void *);
    sigset_t sa_mask;
    int sa_flags;
};

#ifdef __cplusplus
extern "C" {
#endif
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signo);
int sigdelset(sigset_t *set, int signo);
int sigismember(const sigset_t *set, int signo);
int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact);
int kill(pid_t pid, int sig);
#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */
#endif
