/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_SYS_PARAM_H
#define WIN32_POSIX_SYS_PARAM_H
#include <limits.h>
#ifndef MAXPATHLEN
#define MAXPATHLEN 260
#endif
#ifndef PATH_MAX
#define PATH_MAX MAXPATHLEN
#endif
#ifndef NOFILE
#define NOFILE 256
#endif
#endif
