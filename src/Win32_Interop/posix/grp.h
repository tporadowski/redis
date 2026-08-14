/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_GRP_H
#define WIN32_POSIX_GRP_H
struct group {
    char *gr_name;
};
#ifndef gid_t
typedef int gid_t;
#endif
static inline struct group *getgrgid(gid_t gid) { (void)gid; return 0; }
#endif
