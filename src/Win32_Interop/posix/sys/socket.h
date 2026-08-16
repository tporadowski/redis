/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_SYS_SOCKET_H
#define WIN32_POSIX_SYS_SOCKET_H
#include "../../Win32_FDAPI.h"
#ifndef AF_UNIX
#define AF_UNIX 1
#endif
#ifndef AF_LOCAL
#define AF_LOCAL AF_UNIX
#endif
#ifndef SOCK_STREAM
#define SOCK_STREAM 1
#endif
#ifndef SOL_SOCKET
#define SOL_SOCKET 0xffff
#endif
#ifndef SO_REUSEADDR
#define SO_REUSEADDR 0x0004
#endif
#ifndef SO_KEEPALIVE
#define SO_KEEPALIVE 0x0008
#endif
#ifndef SO_ERROR
#define SO_ERROR 0x1007
#endif
#ifndef SO_SNDBUF
#define SO_SNDBUF 0x1001
#endif
#ifndef SO_RCVBUF
#define SO_RCVBUF 0x1002
#endif
#endif
