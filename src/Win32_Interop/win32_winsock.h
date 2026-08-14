/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_INTEROP_WINSOCK_H
#define WIN32_INTEROP_WINSOCK_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

/*
 * Recent Windows SDKs define struct timeval in WinSock2.h with no guard.
 * Redis TUs already have timeval from Win32_Time.h via /FI. Rename the
 * Winsock copy so both headers can be present in the same translation unit.
 */
#ifdef _TIMEVAL_DEFINED
#define timeval winsock_timeval
#endif

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <mswsock.h>
#include <windows.h>

#ifdef _TIMEVAL_DEFINED
#undef timeval
#endif

#endif
