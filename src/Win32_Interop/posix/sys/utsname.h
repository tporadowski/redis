/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_SYS_UTSNAME_H
#define WIN32_POSIX_SYS_UTSNAME_H
struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};
#ifdef __cplusplus
extern "C" {
#endif
int uname(struct utsname *buf);
#ifdef __cplusplus
}
#endif
#endif
