/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#define WIN32_NO_UTF8_IO_REMAP
#define WIN32_NO_RANDOM_REMAP
#include "win32_pre.h"
#include "Win32_Time.h"
#include "Win32_FDAPI.h"
#include "Win32_Error.h"
#include "posix/sys/utsname.h"
#include "posix/sys/uio.h"
#include "posix/dirent.h"
#include "posix/glob.h"
#include "posix/dlfcn.h"
#include "posix/sys/time.h"
#include "posix/sys/file.h"
#include "posix/sys/resource.h"

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <direct.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <wchar.h>
#include <windows.h>

#ifndef UNUSED
#define UNUSED(V) ((void)(V))
#endif

#ifndef __RTL_GENRANDOM
#define __RTL_GENRANDOM 1
typedef BOOLEAN (WINAPI *RtlGenRandomFunc)(void *RandomBuffer,
                                           ULONG RandomBufferLength);
#endif

int win32_get_proc_address(void *module, const char *name,
                           void *function, size_t function_size) {
    const unsigned char *cursor;
    FARPROC raw_function;
    DWORD error;

    if (module == NULL || name == NULL || name[0] == '\0' || function == NULL ||
        function_size != sizeof(raw_function)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    for (cursor = (const unsigned char *)name; *cursor != '\0'; cursor++) {
        if (*cursor > 0x7f) {
            SetLastError(ERROR_INVALID_NAME);
            return -1;
        }
    }

    raw_function = GetProcAddress((HMODULE)module, name);
    if (raw_function == NULL) {
        error = GetLastError();
        if (error == ERROR_SUCCESS) error = ERROR_PROC_NOT_FOUND;
        SetLastError(error);
        return -1;
    }

    memcpy(function, &raw_function, sizeof(raw_function));
    return 0;
}

static INIT_ONCE secure_random_once = INIT_ONCE_STATIC_INIT;
static RtlGenRandomFunc secure_random_function;
static DWORD secure_random_error = ERROR_SUCCESS;

static BOOL CALLBACK initialize_secure_random(PINIT_ONCE once, PVOID parameter,
                                              PVOID *context) {
    HMODULE module;
    RtlGenRandomFunc function;

    UNUSED(once);
    UNUSED(parameter);
    UNUSED(context);

    module = LoadLibraryW(L"advapi32.dll");
    if (module == NULL) {
        secure_random_error = GetLastError();
        return TRUE;
    }
    if (win32_get_proc_address(module, "SystemFunction036", &function,
                               sizeof(function)) != 0) {
        secure_random_error = GetLastError();
        if (secure_random_error == ERROR_SUCCESS)
            secure_random_error = ERROR_PROC_NOT_FOUND;
        FreeLibrary(module);
        return TRUE;
    }
    secure_random_function = function;
    return TRUE;
}

int win32_secure_random_bytes(void *buffer, size_t length) {
    unsigned char *cursor = (unsigned char *)buffer;

    if (length == 0) return 0;
    if (buffer == NULL) {
        errno = EINVAL;
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }

    if (!InitOnceExecuteOnce(&secure_random_once, initialize_secure_random,
                             NULL, NULL)) {
        secure_random_error = GetLastError();
    }
    if (secure_random_function == NULL) {
        DWORD error = secure_random_error == ERROR_SUCCESS ?
                      ERROR_GEN_FAILURE : secure_random_error;
        SetLastError(error);
        errno = win32_errno_from_system_error((int)error);
        return -1;
    }

    while (length != 0) {
        ULONG chunk = length > (size_t)ULONG_MAX ? ULONG_MAX : (ULONG)length;
        if (!secure_random_function(cursor, chunk)) {
            DWORD error = GetLastError();
            if (error == ERROR_SUCCESS) error = ERROR_GEN_FAILURE;
            SetLastError(error);
            errno = win32_errno_from_system_error((int)error);
            return -1;
        }
        cursor += chunk;
        length -= chunk;
    }
    return 0;
}

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
    unsigned int x = 0;
    if (win32_secure_random_bytes(&x, sizeof(x)) != 0) abort();
    return (long)(x >> 1);
}

void srandom(unsigned int seed) {
    /* System RNG; the POSIX seed is unused. */
    UNUSED(seed);
}

static void filetime_to_timeval(const FILETIME *ft, struct timeval *tv) {
    ULARGE_INTEGER li;
    unsigned long long usec;

    li.LowPart = ft->dwLowDateTime;
    li.HighPart = ft->dwHighDateTime;
    usec = li.QuadPart / 10ULL;
    tv->tv_sec = (long)(usec / 1000000ULL);
    tv->tv_usec = (long)(usec % 1000000ULL);
}

int getrusage(int who, struct rusage *ru) {
    FILETIME starttime, exittime, kerneltime, usertime;

    if (ru == NULL) {
        errno = EFAULT;
        return -1;
    }
    memset(ru, 0, sizeof(*ru));

    if (who == RUSAGE_SELF) {
        if (!GetProcessTimes(GetCurrentProcess(), &starttime, &exittime,
                             &kerneltime, &usertime)) {
            errno = win32_errno_from_system_error((int)GetLastError());
            return -1;
        }
    } else if (who == RUSAGE_THREAD) {
        if (!GetThreadTimes(GetCurrentThread(), &starttime, &exittime,
                            &kerneltime, &usertime)) {
            errno = win32_errno_from_system_error((int)GetLastError());
            return -1;
        }
    } else if (who == RUSAGE_CHILDREN) {
        /* Windows does not accumulate waited-child CPU into the parent. */
        return 0;
    } else {
        errno = EINVAL;
        return -1;
    }

    filetime_to_timeval(&kerneltime, &ru->ru_stime);
    filetime_to_timeval(&usertime, &ru->ru_utime);
    return 0;
}

int geteuid(void) {
    return 0;
}

#ifndef _WINSOCKAPI_
int gethostname(char *name, size_t len) {
    DWORD n = (DWORD)len;
    FDAPI_Init();
    if (!GetComputerNameA(name, &n)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}
#endif

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
    HANDLE handle;
    OVERLAPPED overlapped;
    DWORD flags = 0;
    DWORD gle;

    handle = (HANDLE)FDAPI_get_osfhandle(fd);
    if (handle == INVALID_HANDLE_VALUE || handle == (HANDLE)(intptr_t)-1) {
        errno = EBADF;
        return -1;
    }
    memset(&overlapped, 0, sizeof(overlapped));
    if (operation & LOCK_UN) {
        if (!UnlockFileEx(handle, 0, MAXDWORD, MAXDWORD, &overlapped)) {
            errno = win32_errno_from_system_error((int)GetLastError());
            return -1;
        }
        return 0;
    }
    if (operation & LOCK_EX)
        flags |= LOCKFILE_EXCLUSIVE_LOCK;
    if (operation & LOCK_NB)
        flags |= LOCKFILE_FAIL_IMMEDIATELY;
    if (LockFileEx(handle, flags, 0, MAXDWORD, MAXDWORD, &overlapped))
        return 0;
    gle = GetLastError();
    if (gle == ERROR_LOCK_VIOLATION)
        errno = EWOULDBLOCK;
    else
        errno = win32_errno_from_system_error((int)gle);
    return -1;
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
    wchar_t *wide;
    const char *p;
    int abs_path = 0;

    UNUSED(flags);
    g_dlerror_pending = 0;

    if (!filename)
        return (void *)GetModuleHandleW(NULL);

    for (p = filename; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':')
            abs_path = 1;
    }

    wide = win32_utf8_path_to_wide(filename);
    if (!wide) {
        dl_set_error(filename);
        return NULL;
    }

    /* LOAD_WITH_ALTERED_SEARCH_PATH requires a path component. */
    if (abs_path)
        h = LoadLibraryExW(wide, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    else
        h = LoadLibraryW(wide);
    win32_free(wide);
    if (!h) {
        dl_set_error(filename);
        return NULL;
    }
    return (void *)h;
}

void *dlsym(void *handle, const char *symbol) {
    FARPROC p = NULL;

    g_dlerror_pending = 0;
    if (!handle)
        handle = (void *)GetModuleHandleA(NULL);
    if (win32_get_proc_address(handle, symbol, &p, sizeof(p)) != 0) {
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

FILE *replace_fopen(const char *path, const char *mode) {
    wchar_t *wide_path = win32_utf8_path_to_wide(path);
    wchar_t *wide_mode;
    FILE *file;
    int saved_errno;

    if (wide_path == NULL) return NULL;
    wide_mode = win32_utf8_to_wide(mode);
    if (wide_mode == NULL) {
        win32_free(wide_path);
        return NULL;
    }
    file = _wfopen(wide_path, wide_mode);
    saved_errno = errno;
    win32_free(wide_mode);
    win32_free(wide_path);
    errno = saved_errno;
    return file;
}

FILE *replace_freopen(const char *path, const char *mode, FILE *stream) {
    wchar_t *wide_path = win32_utf8_path_to_wide(path);
    wchar_t *wide_mode;
    FILE *file;
    int saved_errno;

    if (wide_path == NULL) return NULL;
    wide_mode = win32_utf8_to_wide(mode);
    if (wide_mode == NULL) {
        win32_free(wide_path);
        return NULL;
    }
    file = _wfreopen(wide_path, wide_mode, stream);
    saved_errno = errno;
    win32_free(wide_mode);
    win32_free(wide_path);
    errno = saved_errno;
    return file;
}

int replace_unlink(const char *path) {
    wchar_t *wide_path = win32_utf8_path_to_wide(path);
    int result;
    int saved_errno;
    if (wide_path == NULL) return -1;
    result = _wunlink(wide_path);
    saved_errno = errno;
    win32_free(wide_path);
    errno = saved_errno;
    return result;
}

int replace_remove(const char *path) {
    return replace_unlink(path);
}

int replace_mkdir(const char *path) {
    wchar_t *wide_path = win32_utf8_directory_path_to_wide(path);
    int result;
    int saved_errno;
    if (wide_path == NULL) return -1;
    result = _wmkdir(wide_path);
    saved_errno = errno;
    win32_free(wide_path);
    errno = saved_errno;
    return result;
}

int replace_rmdir(const char *path) {
    wchar_t *wide_path = win32_utf8_directory_path_to_wide(path);
    int result;
    int saved_errno;
    if (wide_path == NULL) return -1;
    result = _wrmdir(wide_path);
    saved_errno = errno;
    win32_free(wide_path);
    errno = saved_errno;
    return result;
}

int replace_chmod(const char *path, int mode) {
    wchar_t *wide_path = win32_utf8_path_to_wide(path);
    int result;
    int saved_errno;
    if (wide_path == NULL) return -1;
    result = _wchmod(wide_path, mode);
    saved_errno = errno;
    win32_free(wide_path);
    errno = saved_errno;
    return result;
}

int replace_access(const char *path, int mode) {
    wchar_t *wide_path = win32_utf8_path_to_wide(path);
    int result;
    int saved_errno;
    if (wide_path == NULL) return -1;
    result = _waccess(wide_path, mode);
    saved_errno = errno;
    win32_free(wide_path);
    errno = saved_errno;
    return result;
}

int replace_stat(const char *path, struct stat *buffer) {
    wchar_t *wide_path = win32_utf8_path_to_wide(path);
    struct __stat64 st;
    int result;
    int saved_errno;

    if (!buffer) {
        errno = EINVAL;
        return -1;
    }
    if (wide_path == NULL) return -1;
    result = _wstat64(wide_path, &st);
    saved_errno = errno;
    win32_free(wide_path);
    if (result != 0) {
        errno = saved_errno;
        return -1;
    }
    memset(buffer, 0, sizeof(*buffer));
    buffer->st_mode = st.st_mode;
    buffer->st_nlink = st.st_nlink;
    buffer->st_size = (off_t)st.st_size;
    buffer->st_atime = (time_t)st.st_atime;
    buffer->st_mtime = (time_t)st.st_mtime;
    buffer->st_ctime = (time_t)st.st_ctime;
    return 0;
}

int glob(const char *pattern, int flags,
         int (*errfunc)(const char *, int), glob_t *pglob) {
    char **paths = NULL;
    size_t count = 0;

    UNUSED(flags);
    UNUSED(errfunc);
    if (!pglob) {
        errno = EINVAL;
        return -1;
    }
    pglob->gl_pathc = 0;
    pglob->gl_pathv = NULL;
    if (win32_glob_utf8(pattern, &paths, &count) != 0)
        return GLOB_NOMATCH;
    if (count == 0) {
        win32_globfree_utf8(paths, count);
        return GLOB_NOMATCH;
    }
    pglob->gl_pathc = count;
    pglob->gl_pathv = paths;
    return 0;
}

void globfree(glob_t *pglob) {
    if (!pglob) return;
    win32_globfree_utf8(pglob->gl_pathv, pglob->gl_pathc);
    pglob->gl_pathc = 0;
    pglob->gl_pathv = NULL;
}

int link(const char *oldpath, const char *newpath) {
    wchar_t *wide_src = win32_utf8_path_to_wide(oldpath);
    wchar_t *wide_dst;

    if (wide_src == NULL) return -1;
    wide_dst = win32_utf8_path_to_wide(newpath);
    if (wide_dst == NULL) {
        win32_free(wide_src);
        return -1;
    }

    if (CreateHardLinkW(wide_dst, wide_src, NULL)) {
        win32_free(wide_src);
        win32_free(wide_dst);
        return 0;
    }

    {
        DWORD e = GetLastError();
        win32_free(wide_src);
        win32_free(wide_dst);
        errno = win32_errno_from_system_error((int)e);
        return -1;
    }
}

int truncate(const char *path, off_t length) {
    wchar_t *wide_path = win32_utf8_path_to_wide(path);
    HANDLE handle;
    LARGE_INTEGER newSize;
    int result = 0;

    if (wide_path == NULL) return -1;
    handle = CreateFileW(wide_path, GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, 0, NULL);
    win32_free(wide_path);
    if (handle == INVALID_HANDLE_VALUE) {
        set_errno_from_last_error();
        return -1;
    }
    newSize.QuadPart = length;
    if (!SetFilePointerEx(handle, newSize, NULL, FILE_BEGIN) ||
        !SetEndOfFile(handle)) {
        set_errno_from_last_error();
        result = -1;
    }
    CloseHandle(handle);
    return result;
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
    WIN32_FIND_DATAW ffd;
    int stored;
    int done;
    struct dirent cur;
} DIR;

DIR *opendir(const char *name) {
    DIR *d;
    size_t length;
    char *pattern;
    wchar_t *wide;

    if (!name) {
        errno = EINVAL;
        return NULL;
    }
    length = strlen(name);
    pattern = (char *)malloc(length + 3);
    if (!pattern) {
        errno = ENOMEM;
        return NULL;
    }
    memcpy(pattern, name, length);
    if (length != 0 && name[length - 1] != '/' && name[length - 1] != '\\')
        pattern[length++] = '/';
    pattern[length++] = '*';
    pattern[length] = '\0';
    wide = win32_utf8_path_to_wide(pattern);
    free(pattern);
    if (!wide) return NULL;

    d = (DIR *)calloc(1, sizeof(*d));
    if (!d) {
        win32_free(wide);
        errno = ENOMEM;
        return NULL;
    }
    d->handle = FindFirstFileW(wide, &d->ffd);
    win32_free(wide);
    if (d->handle == INVALID_HANDLE_VALUE) {
        free(d);
        set_errno_from_last_error();
        return NULL;
    }
    d->stored = 1;
    return d;
}

struct dirent *readdir(DIR *dirp) {
    char *utf8;
    if (!dirp || dirp->done) return NULL;
    if (!dirp->stored) {
        if (!FindNextFileW(dirp->handle, &dirp->ffd)) {
            dirp->done = 1;
            return NULL;
        }
    }
    dirp->stored = 0;
    memset(&dirp->cur, 0, sizeof(dirp->cur));
    utf8 = win32_wide_to_utf8(dirp->ffd.cFileName);
    if (utf8) {
        strncpy(dirp->cur.d_name, utf8, sizeof(dirp->cur.d_name) - 1);
        win32_free(utf8);
    }
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
