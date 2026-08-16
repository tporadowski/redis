/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_INTEROP_PROCESS_TABLE_H
#define WIN32_INTEROP_PROCESS_TABLE_H

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "win32_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WP_QFORK = 1,
    WP_SENTINEL_SCRIPT = 2
} WinPidKind;

void winpid_register(pid_t pid, HANDLE process, WinPidKind kind, HANDLE abort_event);
void winpid_unregister(pid_t pid);

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */
#endif
