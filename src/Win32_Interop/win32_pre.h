/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/*
 * Forced include for Redis src/ on Windows (see CMake /FI).
 * Defines _OFF_T_DEFINED before any CRT header can install a 32-bit off_t.
 */
#ifndef WIN32_INTEROP_PRE_H
#define WIN32_INTEROP_PRE_H

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00 /* Windows 10 */
#endif

#include "win32_types.h"
#include "Win32_Portability.h"

#ifndef F_GETFL
#define F_GETFL 3
#endif
#ifndef F_SETFL
#define F_SETFL 4
#endif
#ifndef F_GETFD
#define F_GETFD 1
#endif
#ifndef F_SETFD
#define F_SETFD 2
#endif
#ifndef FD_CLOEXEC
#define FD_CLOEXEC 1
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x0004
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0x80000
#endif

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#ifndef SIGHUP
#define SIGHUP  1
#endif
#ifndef SIGQUIT
#define SIGQUIT 3
#endif
#ifndef SIGBUS
#define SIGBUS  7
#endif
#ifndef SIGKILL
#define SIGKILL 9
#endif
#ifndef SIGUSR1
#define SIGUSR1 10
#endif
#ifndef SIGUSR2
#define SIGUSR2 12
#endif
#ifndef SIGPIPE
#define SIGPIPE 13
#endif
#ifndef SIGALRM
#define SIGALRM 14
#endif
#ifndef SIGCHLD
#define SIGCHLD 17
#endif
#ifndef SIGSTOP
#define SIGSTOP 19
#endif
#ifndef SIGCONT
#define SIGCONT 18
#endif
#ifndef SIGTSTP
#define SIGTSTP 20
#endif
#ifndef ESOCKTNOSUPPORT
#define ESOCKTNOSUPPORT 10044
#endif
#ifndef EPFNOSUPPORT
#define EPFNOSUPPORT 10046
#endif
#ifndef ENOTSUP
#define ENOTSUP 129
#endif
#ifndef _SC_PAGESIZE
#define _SC_PAGESIZE 30
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_NPROCESSORS_ONLN 84
#define _SC_CLK_TCK 2
#define _SC_GETPW_R_SIZE_MAX 70
#endif

#include "Win32_Time.h"
/* Winsock before OpenSSL/e_ostime.h (and before FDAPI remaps). openssl/ssl.h
 * includes winsock2.h; if that is the first include, #define connect etc.
 * rewrite the SDK prototypes. */
#include "win32_winsock.h"
#include "Win32_Signal.h"

#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif

#include <sys/stat.h>
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISFIFO
#define S_ISFIFO(m) (((m) & _S_IFMT) == _S_IFIFO)
#endif
#ifndef S_IXUSR
#define S_IXUSR _S_IEXEC
#define S_IXGRP _S_IEXEC
#define S_IXOTH _S_IEXEC
#define S_IRUSR _S_IREAD
#define S_IWUSR _S_IWRITE
#define S_IRGRP _S_IREAD
#define S_IROTH _S_IREAD
#endif

#endif /* _WIN32 */

#endif
