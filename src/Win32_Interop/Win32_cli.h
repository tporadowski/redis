/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_INTEROP_CLI_H
#define WIN32_INTEROP_CLI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Console VT, UTF-8, and HOME for redis-cli. Call once from main. */
void cliWin32Init(void);

#ifdef __cplusplus
}
#endif

#endif
