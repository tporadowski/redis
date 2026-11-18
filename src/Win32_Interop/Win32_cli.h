/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_INTEROP_CLI_H
#define WIN32_INTEROP_CLI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Console VT, UTF-8, and HOME for redis-cli. Call once from main. */
void cliWin32Init(void);

/* Wait up to timeout_ms for the hiredis SOCKET (a Winsock handle, not an
 * RFD) or for unread stdin. Returns 1 (socket), 2 (stdin), 0 (timeout),
 * or -1 (error). Does not consume stdin. */
int cliWin32WaitSocketOrStdin(long long sock, int timeout_ms);

/* Poll a hiredis SOCKET. mask and the result use AE_READABLE (1) and
 * AE_WRITABLE (2). Returns 0 on timeout and -1 on error. */
int cliWin32PollSocket(long long sock, int mask, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
