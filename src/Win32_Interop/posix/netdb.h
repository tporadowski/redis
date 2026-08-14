/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_NETDB_H
#define WIN32_POSIX_NETDB_H
#include "../Win32_FDAPI.h"
#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif
#ifndef NI_MAXSERV
#define NI_MAXSERV 32
#endif
#ifndef NI_NUMERICHOST
#define NI_NUMERICHOST 0x02
#endif
#ifndef NI_NUMERICSERV
#define NI_NUMERICSERV 0x08
#endif
#ifndef EAI_FAIL
#define EAI_FAIL 4
#endif
#ifndef EAI_AGAIN
#define EAI_AGAIN 2
#endif
#ifndef EAI_NONAME
#define EAI_NONAME 8
#endif
#endif
