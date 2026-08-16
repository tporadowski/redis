/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_LIBGEN_H
#define WIN32_POSIX_LIBGEN_H
#ifdef __cplusplus
extern "C" {
#endif
char *basename(char *path);
char *dirname(char *path);
#ifdef __cplusplus
}
#endif
#endif
