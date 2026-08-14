/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_SYS_UN_H
#define WIN32_POSIX_SYS_UN_H
#include "../../win32_pre.h"
#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif
struct sockaddr_un {
    unsigned short sun_family;
    char sun_path[UNIX_PATH_MAX];
};
#endif
