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
#ifndef rename
#define rename fdapi_rename
#endif
#ifndef getpid
#define getpid _getpid
#endif
#ifndef getppid
#define getppid win32_getppid
#endif

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <sys/stat.h>
FILE *replace_fopen(const char *path, const char *mode);
FILE *replace_freopen(const char *path, const char *mode, FILE *stream);
int replace_unlink(const char *path);
int replace_remove(const char *path);
int replace_mkdir(const char *path);
int replace_rmdir(const char *path);
int replace_chmod(const char *path, int mode);
int replace_access(const char *path, int mode);
int replace_stat(const char *path, struct stat *buffer);
char *win32_getcwd(char *buf, size_t size);
int win32_set_current_directory_utf8(const char *path);

#ifndef WIN32_NO_UTF8_IO_REMAP
#undef fopen
#define fopen replace_fopen
#undef freopen
#define freopen replace_freopen
#undef remove
#define remove replace_remove
#undef unlink
#define unlink replace_unlink
#undef getcwd
#define getcwd win32_getcwd
#undef chdir
#define chdir win32_set_current_directory_utf8
#undef rmdir
#define rmdir replace_rmdir
#undef mkdir
#define mkdir(path, mode) replace_mkdir(path)
#undef access
#define access replace_access
#undef lstat
#define lstat replace_stat
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
