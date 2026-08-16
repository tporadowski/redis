/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#include "win32_pre.h"
#include "Win32_Time.h"
#include "Win32_FDAPI.h"
#include "posix/sys/utsname.h"
#include "posix/sys/uio.h"
#include "posix/dirent.h"
#include "posix/dlfcn.h"
#include "posix/sys/time.h"

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <windows.h>

#ifndef UNUSED
#define UNUSED(V) ((void)(V))
#endif

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME  0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

typedef int clockid_t;

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    if (!tp) {
        errno = EFAULT;
        return -1;
    }
    if (clk_id == CLOCK_MONOTONIC) {
        uint64_t us = GetHighResRelativeTime(1000000.0);
        tp->tv_sec = (time_t)(us / 1000000ULL);
        tp->tv_nsec = (long)((us % 1000000ULL) * 1000ULL);
        return 0;
    }
    {
        struct timeval tv;
        gettimeofday_highres(&tv, NULL);
        tp->tv_sec = tv.tv_sec;
        tp->tv_nsec = tv.tv_usec * 1000L;
        return 0;
    }
}

unsigned int sleep(unsigned int seconds) {
    Sleep(seconds * 1000U);
    return 0;
}

int usleep(unsigned int usec) {
    if (usec == 0) {
        Sleep(0);
        return 0;
    }
    Sleep((usec + 999) / 1000);
    return 0;
}

int mkstemp(char *template) {
    if (!_mktemp(template)) return -1;
    /* Must return an RFD so fdapi_write/fsync/close hit the CRT handle. */
    return fdapi_open(template, O_RDWR | O_CREAT | O_EXCL, 0600);
}

long random(void) {
    return (long)rand();
}

void srandom(unsigned int seed) {
    srand(seed);
}

int geteuid(void) {
    return 0;
}

int gethostname(char *name, size_t len) {
    DWORD n = (DWORD)len;
    FDAPI_Init();
    if (!GetComputerNameA(name, &n)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

pid_t fork(void) {
    errno = ENOSYS;
    return -1;
}

pid_t win32_getppid(void) {
    return 0;
}

long sysconf(int name) {
    if (name == _SC_PAGESIZE || name == _SC_PAGE_SIZE)
        return 4096;
    if (name == _SC_NPROCESSORS_ONLN) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        return (long)si.dwNumberOfProcessors;
    }
    if (name == _SC_CLK_TCK)
        return 1000;
    errno = EINVAL;
    return -1;
}

int unsetenv(const char *name) {
    return _putenv_s(name, "") == 0 ? 0 : -1;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!overwrite && getenv(name))
        return 0;
    return _putenv_s(name, value ? value : "") == 0 ? 0 : -1;
}

struct tm *localtime_r(const time_t *timep, struct tm *result) {
    if (localtime_s(result, timep) != 0)
        return NULL;
    return result;
}

struct tm *gmtime_r(const time_t *timep, struct tm *result) {
    if (gmtime_s(result, timep) != 0)
        return NULL;
    return result;
}

/* kill() is implemented in Win32_ProcessTable.c */

int flock(int fd, int operation) {
    UNUSED(fd);
    UNUSED(operation);
    return 0;
}

int uname(struct utsname *buf) {
    if (!buf) {
        errno = EFAULT;
        return -1;
    }
    memset(buf, 0, sizeof(*buf));
    strncpy(buf->sysname, "Windows", sizeof(buf->sysname) - 1);
    strncpy(buf->release, "10", sizeof(buf->release) - 1);
    strncpy(buf->version, "10.0", sizeof(buf->version) - 1);
#if defined(_M_X64) || defined(__x86_64__)
    strncpy(buf->machine, "x86_64", sizeof(buf->machine) - 1);
#else
    strncpy(buf->machine, "unknown", sizeof(buf->machine) - 1);
#endif
    {
        DWORD n = (DWORD)sizeof(buf->nodename);
        GetComputerNameA(buf->nodename, &n);
    }
    return 0;
}

int writev(int fd, const struct iovec *iov, int iovcnt) {
    int i;
    ssize_t total = 0;
    if (!iov || iovcnt < 0) {
        errno = EINVAL;
        return -1;
    }
    for (i = 0; i < iovcnt; i++) {
        ssize_t n = fdapi_write(fd, iov[i].iov_base, iov[i].iov_len);
        if (n < 0)
            return total > 0 ? (int)total : -1;
        total += n;
        if ((size_t)n < iov[i].iov_len)
            break;
    }
    return (int)total;
}

int sigemptyset(sigset_t *set) {
    if (set) *set = 0;
    return 0;
}

int sigfillset(sigset_t *set) {
    if (set) *set = (sigset_t)-1;
    return 0;
}

int sigaddset(sigset_t *set, int signo) {
    if (!set) return -1;
    *set |= (sigset_t)1 << (signo & 31);
    return 0;
}

int sigdelset(sigset_t *set, int signo) {
    if (!set) return -1;
    *set &= ~((sigset_t)1 << (signo & 31));
    return 0;
}

int sigismember(const sigset_t *set, int signo) {
    if (!set) return 0;
    return (*set & ((sigset_t)1 << (signo & 31))) != 0;
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *oldact) {
    void (*prev)(int) = SIG_DFL;
    if (oldact)
        memset(oldact, 0, sizeof(*oldact));
    if (!act)
        return 0;
    if (sig == SIGINT || sig == SIGTERM || sig == SIGABRT) {
        prev = signal(sig, act->sa_handler);
        if (prev == SIG_ERR)
            return -1;
        if (oldact) oldact->sa_handler = prev;
        return 0;
    }
    /* SIGHUP/SIGPIPE/SIGUSR1/… are no-ops on Windows. */
    return 0;
}

static char g_dlerror[512];
static int g_dlerror_pending;

static void dl_set_error(const char *what) {
    DWORD gle = GetLastError();
    char winmsg[384];
    DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                             NULL, gle, 0, winmsg, sizeof(winmsg), NULL);
    while (n && (winmsg[n - 1] == '\r' || winmsg[n - 1] == '\n'))
        winmsg[--n] = '\0';
    snprintf(g_dlerror, sizeof(g_dlerror), "%s: %s (gle=%lu)",
             what ? what : "dlfcn", n ? winmsg : "unknown", gle);
    g_dlerror_pending = 1;
}

void *dlopen(const char *filename, int flags) {
    HMODULE h;
    char path[MAX_PATH];
    size_t i;
    int abs_path = 0;

    UNUSED(flags);
    g_dlerror_pending = 0;

    if (!filename)
        return (void *)GetModuleHandleA(NULL);

    for (i = 0; i < MAX_PATH - 1 && filename[i]; i++) {
        char c = filename[i];
        if (c == '/')
            c = '\\';
        if (c == '\\' || c == ':')
            abs_path = 1;
        path[i] = c;
    }
    path[i] = '\0';

    /* LOAD_WITH_ALTERED_SEARCH_PATH requires a path component. */
    if (abs_path)
        h = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    else
        h = LoadLibraryA(path);
    if (!h) {
        dl_set_error(path);
        return NULL;
    }
    return (void *)h;
}

void *dlsym(void *handle, const char *symbol) {
    FARPROC p;

    g_dlerror_pending = 0;
    if (!handle)
        handle = (void *)GetModuleHandleA(NULL);
    p = GetProcAddress((HMODULE)handle, symbol);
    if (!p) {
        dl_set_error(symbol);
        return NULL;
    }
    return (void *)p;
}

int dlclose(void *handle) {
    g_dlerror_pending = 0;
    if (!handle)
        return 0;
    if (FreeLibrary((HMODULE)handle))
        return 0;
    dl_set_error("dlclose");
    return -1;
}

char *dlerror(void) {
    if (!g_dlerror_pending)
        return NULL;
    g_dlerror_pending = 0;
    return g_dlerror;
}

int dladdr(const void *addr, Dl_info *info) {
    HMODULE h = NULL;
    static char fname[MAX_PATH];

    if (!info)
        return 0;
    memset(info, 0, sizeof(*info));
    if (!addr)
        return 0;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)addr, &h) ||
        !h)
        return 0;
    if (!GetModuleFileNameA(h, fname, MAX_PATH))
        return 0;
    info->dli_fname = fname;
    info->dli_fbase = (void *)h;
    info->dli_sname = NULL;
    info->dli_saddr = (void *)addr;
    return 1;
}

int fchmod(int fd, int mode) {
    UNUSED(fd);
    UNUSED(mode);
    return 0;
}

int link(const char *oldpath, const char *newpath) {
    if (CreateHardLinkA(newpath, oldpath, NULL))
        return 0;
    if (CopyFileA(oldpath, newpath, TRUE))
        return 0;
    {
        DWORD e = GetLastError();
        if (e == ERROR_ALREADY_EXISTS)
            errno = EEXIST;
        else if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
            errno = ENOENT;
        else if (e == ERROR_ACCESS_DENIED)
            errno = EACCES;
        else
            errno = EIO;
    }
    return -1;
}

int truncate(const char *path, off_t length) {
    int fd = _open(path, _O_RDWR | _O_BINARY);
    int rc;
    if (fd < 0) return -1;
    rc = _chsize_s(fd, length);
    _close(fd);
    return rc;
}

int setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value) {
    UNUSED(which);
    if (old_value) memset(old_value, 0, sizeof(*old_value));
    UNUSED(new_value);
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    DWORD ms;
    if (!req) {
        errno = EFAULT;
        return -1;
    }
    ms = (DWORD)(req->tv_sec * 1000 + req->tv_nsec / 1000000);
    if (ms == 0 && (req->tv_sec || req->tv_nsec)) ms = 1;
    Sleep(ms);
    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

typedef struct DIR {
    HANDLE handle;
    WIN32_FIND_DATAA ffd;
    int stored;
    int done;
    struct dirent cur;
} DIR;

DIR *opendir(const char *name) {
    DIR *d;
    char pattern[MAX_PATH];
    if (!name) {
        errno = EINVAL;
        return NULL;
    }
    d = (DIR *)calloc(1, sizeof(*d));
    if (!d) return NULL;
    snprintf(pattern, sizeof(pattern), "%s\\*", name);
    d->handle = FindFirstFileA(pattern, &d->ffd);
    if (d->handle == INVALID_HANDLE_VALUE) {
        free(d);
        errno = ENOENT;
        return NULL;
    }
    d->stored = 1;
    return d;
}

struct dirent *readdir(DIR *dirp) {
    if (!dirp || dirp->done) return NULL;
    if (!dirp->stored) {
        if (!FindNextFileA(dirp->handle, &dirp->ffd)) {
            dirp->done = 1;
            return NULL;
        }
    }
    dirp->stored = 0;
    memset(&dirp->cur, 0, sizeof(dirp->cur));
    strncpy(dirp->cur.d_name, dirp->ffd.cFileName, sizeof(dirp->cur.d_name) - 1);
    dirp->cur.d_type = (dirp->ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;
    return &dirp->cur;
}

char *basename(char *path) {
    char *p, *slash = path;
    if (!path || !*path) return ".";
    for (p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            slash = p + 1;
    }
    return *slash ? slash : path;
}

char *dirname(char *path) {
    char *p;
    if (!path || !*path) return ".";
    for (p = path + strlen(path) - 1; p > path; p--) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
            return path;
        }
    }
    return ".";
}

int closedir(DIR *dirp) {
    if (!dirp) return -1;
    if (dirp->handle && dirp->handle != INVALID_HANDLE_VALUE)
        FindClose(dirp->handle);
    free(dirp);
    return 0;
}
