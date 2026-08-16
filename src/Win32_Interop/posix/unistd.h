/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_UNISTD_H
#define WIN32_POSIX_UNISTD_H

#include "../win32_pre.h"
#include <stddef.h>
#include <io.h>
#include <process.h>
#include <direct.h>
#include "../Win32_FDAPI.h"

#ifndef open
#define open fdapi_open
#endif
#ifndef close
#define close fdapi_close
#endif
#ifndef read
#define read fdapi_read
#endif
#ifndef write
#define write fdapi_write
#endif
#ifndef isatty
#define isatty fdapi_isatty
#endif
#ifndef lseek
#define lseek fdapi_lseek
#endif
#ifndef umask
#define umask _umask
#endif
#ifndef access
#define access _access
#endif
#ifndef unlink
#define unlink _unlink
#endif
#ifndef getpid
#define getpid _getpid
#endif
#ifndef getppid
#define getppid win32_getppid
#endif
#ifndef getcwd
#define getcwd _getcwd
#endif
#ifndef chdir
#define chdir _chdir
#endif
#ifndef rmdir
#define rmdir _rmdir
#endif
#ifndef mkdir
#define mkdir(path, mode) _mkdir(path)
#endif
#ifndef lstat
#define lstat stat
#endif

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);
int mkstemp(char *template);
int geteuid(void);
#ifndef _WINSOCKAPI_
int gethostname(char *name, size_t len);
#endif
long random(void);
void srandom(unsigned int seed);
pid_t fork(void);
pid_t win32_getppid(void);
int fsync(int fd);
int pipe(int pipefd[2]);
int ftruncate(int fd, off_t length);
long sysconf(int name);
int unsetenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
char *strptime(const char *s, const char *format, struct tm *tm);
struct tm *localtime_r(const time_t *timep, struct tm *result);
struct tm *gmtime_r(const time_t *timep, struct tm *result);
int fchmod(int fd, int mode);
int link(const char *oldpath, const char *newpath);
int truncate(const char *path, off_t length);
int nanosleep(const struct timespec *req, struct timespec *rem);

#ifndef ftello
#define ftello _ftelli64
#endif
#ifndef fseeko
#define fseeko _fseeki64
#endif

#ifndef _SC_PAGESIZE
#define _SC_PAGESIZE 30
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_NPROCESSORS_ONLN 84
#define _SC_CLK_TCK 2
#define _SC_GETPW_R_SIZE_MAX 70
#endif

#ifdef __cplusplus
}
#endif

#endif
