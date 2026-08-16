/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_FCNTL_H
#define WIN32_POSIX_FCNTL_H
#include_next <fcntl.h>
#ifdef open
#undef open
#endif
#define open fdapi_open
#ifdef __cplusplus
extern "C" {
#endif
int fdapi_open(const char *pathname, int flags, ...);
#ifdef __cplusplus
}
#endif
#endif
