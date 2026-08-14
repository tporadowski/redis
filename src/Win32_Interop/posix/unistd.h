/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_UNISTD_H
#define WIN32_POSIX_UNISTD_H

#include "../win32_pre.h"
#include <stddef.h>
#include <io.h>
#include <process.h>
#include "../Win32_FDAPI.h"

#ifndef close
#define close _close
#endif
#ifndef read
#define read _read
#endif
#ifndef write
#define write _write
#endif
#ifndef isatty
#define isatty _isatty
#endif
#ifndef lseek
#define lseek _lseeki64
#endif

#ifdef __cplusplus
extern "C" {
#endif

unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);
int mkstemp(char *template);
int geteuid(void);
int gethostname(char *name, size_t len);
long random(void);
void srandom(unsigned int seed);

#ifdef __cplusplus
}
#endif

#endif
