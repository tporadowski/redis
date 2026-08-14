/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_NETINET_TCP_H
#define WIN32_POSIX_NETINET_TCP_H
#include "../../Win32_FDAPI.h"
#ifndef TCP_NODELAY
#define TCP_NODELAY 0x0001
#endif
#ifndef TCP_KEEPIDLE
#define TCP_KEEPIDLE 3
#endif
#ifndef TCP_KEEPINTVL
#define TCP_KEEPINTVL 17
#endif
#ifndef TCP_KEEPCNT
#define TCP_KEEPCNT 16
#endif
#endif
