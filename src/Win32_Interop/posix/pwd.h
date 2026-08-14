/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_PWD_H
#define WIN32_POSIX_PWD_H
#include "../../win32_types.h"
#ifndef uid_t
typedef int uid_t;
#endif
struct passwd {
    char *pw_name;
    char *pw_dir;
    uid_t pw_uid;
};
static inline struct passwd *getpwuid(uid_t uid) { (void)uid; return 0; }
static inline struct passwd *getpwnam(const char *name) { (void)name; return 0; }
#endif
