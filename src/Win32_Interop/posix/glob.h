/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_GLOB_H
#define WIN32_POSIX_GLOB_H
#include <stddef.h>
#define GLOB_ERR 0x01
#define GLOB_MARK 0x02
#define GLOB_NOSORT 0x04
#define GLOB_DOOFFS 0x08
#define GLOB_NOCHECK 0x10
#define GLOB_APPEND 0x20
#define GLOB_NOESCAPE 0x40
#define GLOB_NOMATCH 3
typedef struct {
    size_t gl_pathc;
    char **gl_pathv;
    size_t gl_offs;
} glob_t;
static inline int glob(const char *pattern, int flags,
                       int (*errfunc)(const char *, int), glob_t *pglob) {
    (void)pattern; (void)flags; (void)errfunc;
    if (pglob) { pglob->gl_pathc = 0; pglob->gl_pathv = 0; }
    return GLOB_NOMATCH;
}
static inline void globfree(glob_t *pglob) { (void)pglob; }
#endif
