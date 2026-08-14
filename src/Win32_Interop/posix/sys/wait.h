/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_SYS_WAIT_H
#define WIN32_POSIX_SYS_WAIT_H
#include "../../win32_types.h"
#define WNOHANG 1
#define WUNTRACED 2
#define WIFEXITED(s)   (((s) & 0x7f) == 0)
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WIFSIGNALED(s) (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define WTERMSIG(s)    ((s) & 0x7f)
#define WIFSTOPPED(s)  (((s) & 0xff) == 0x7f)
static inline pid_t waitpid(pid_t pid, int *status, int options) {
    (void)pid; (void)options;
    if (status) *status = 0;
    return -1;
}
#endif
