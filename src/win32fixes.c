#ifdef _WIN32

#include <process.h>
#include <stdlib.h>
#include <errno.h>
#ifndef FD_SETSIZE
#define FD_SETSIZE 16000
#endif
#include <winsock2.h>
#include <windows.h>
#include <signal.h>
#include <time.h>
#include <locale.h>
#include <math.h>
#include "win32fixes.h"


/* Redefined here to avoid redis.h so it can be used in other projects */
#define REDIS_NOTUSED(V) ((void) V)

/* Winsock requires library initialization on startup  */
int w32initWinSock(void) {

    WSADATA t_wsa;
    WORD wVers;
    int iError;

    wVers = MAKEWORD(2, 2);
    iError = WSAStartup(wVers, &t_wsa);

    if(iError != NO_ERROR || LOBYTE(t_wsa.wVersion) != 2 || HIBYTE(t_wsa.wVersion) != 2 ) {
        return 0; /* not done; check WSAGetLastError() for error number */
    };

    return 1;
}

/* Placeholder for terminating forked process. */
/* fork() is nonexistatn on windows, background cmds are todo */
int w32CeaseAndDesist(pid_t pid) {

    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);

    /* invalid process; no access rights; etc   */
    if (h == NULL)
        return errno = EINVAL;

    if (!TerminateProcess(h, 127))
        return errno = EINVAL;

    errno = WaitForSingleObject(h, INFINITE);
    CloseHandle(h);

    return 0;
}

/* Behaves as posix, works without ifdefs, makes compiler happy */
int sigaction(int sig, struct sigaction *in, struct sigaction *out) {
    REDIS_NOTUSED(out);

    /* When the SA_SIGINFO flag is set in sa_flags then sa_sigaction
     * is used. Otherwise, sa_handler is used */
    if (in->sa_flags & SA_SIGINFO)
        signal(sig, in->sa_sigaction);
    else
        signal(sig, in->sa_handler);

    return 0;
}

/* Terminates process, implemented only for SIGKILL */
int kill(pid_t pid, int sig) {

    if (sig == SIGKILL) {

        HANDLE h = OpenProcess(PROCESS_TERMINATE, 0, pid);

        if (!TerminateProcess(h, 127)) {
            errno = EINVAL; /* GetLastError() */
            CloseHandle(h);
            return -1;
        };

        CloseHandle(h);
        return 0;
    } else {
        errno = EINVAL;
        return -1;
    };
}

/* Forced write to disk */
int fsync (int fd) {
    HANDLE h = (HANDLE) _get_osfhandle(fd);
    DWORD err;

    if (h == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }

    if (!FlushFileBuffers(h)) {
        /* Windows error -> Unix */
        err = GetLastError();
        switch (err) {
            case ERROR_INVALID_HANDLE:
            errno = EINVAL;
            break;

            default:
            errno = EIO;
        }
        return -1;
    }

    return 0;
}

/* Missing wait3() implementation */
pid_t wait3(int *stat_loc, int options, void *rusage) {
    REDIS_NOTUSED(stat_loc);
    REDIS_NOTUSED(options);
    REDIS_NOTUSED(rusage);
    return (pid_t) waitpid((intptr_t) -1, 0, WAIT_FLAGS);
}

/* Replace MS C rtl rand which is 15bit with 32 bit */
int replace_random() {
#if defined(_WIN64) || defined(_MSC_VER)
    unsigned int x=0;
    RtlGenRandom(&x, sizeof(UINT_MAX));
    return (int)(x >> 1);
#else
    unsigned int x=0;
    RtlGenRandom(&x, sizeof(UINT_MAX));
    return (int)(x >> 1);
#endif
}

/* BSD sockets compatibile replacement */
int replace_setsockopt(int socket, int level, int optname, const void *optval, socklen_t optlen) {
    return (setsockopt)((SOCKET)socket, level, optname, optval, optlen);
}

/* set size with 64bit support */
int replace_ftruncate(int fd, off64_t length) {
    HANDLE h = (HANDLE) _get_osfhandle (fd);
    LARGE_INTEGER l, o;

    if (h == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }

    l.QuadPart = length;

    if (!SetFilePointerEx(h, l, &o, FILE_BEGIN)) return -1;
    if (!SetEndOfFile(h)) return -1;

    return 0;
}

/* Rename which works on Windows when file exists */
int replace_rename(const char *src, const char *dst) {
    /* anti-virus may lock file - error code 5. Retry until it works or get a different error */
    int retries = 50;
    while (1) {
        if (MoveFileEx(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH)) {
            return 0;
        } else {
            errno = GetLastError();
            if (errno != 5) break;
            retries--;
            if (retries == 0) {
                retries = 50;
                Sleep(10);
            }
        }
    }
    /* On error we will return generic error code without GetLastError() */
    return -1;
}

int pthread_sigmask(int how, const sigset_t *set, sigset_t *oset) {
    REDIS_NOTUSED(set);
    REDIS_NOTUSED(oset);
    switch (how) {
      case SIG_BLOCK:
      case SIG_UNBLOCK:
      case SIG_SETMASK:
           break;
      default:
            errno = EINVAL;
            return -1;
    }

  errno = ENOSYS;
  return -1;
}


/* Redis forks to perform background writing */
/* fork() on unix will split process in two */
/* marking memory pages as Copy-On-Write so */
/* child process will have data snapshot.   */
/* Windows has no support for fork().       */
int fork(void) {
  return -1;
 }

/* Redis CPU GetProcessTimes -> rusage  */
int getrusage(int who, struct rusage * r) {

   FILETIME starttime, exittime, kerneltime, usertime;
   ULARGE_INTEGER li;

   if (r == NULL) {
       errno = EFAULT;
       return -1;
   }

   memset(r, 0, sizeof(struct rusage));

   if (who == RUSAGE_SELF) {
     if (!GetProcessTimes(GetCurrentProcess(),
                        &starttime,
                        &exittime,
                        &kerneltime,
                        &usertime))
     {
         errno = EFAULT;
         return -1;
     }
   }

   if (who == RUSAGE_CHILDREN) {
        /* Childless on windows */
        starttime.dwLowDateTime = 0;
        starttime.dwHighDateTime = 0;
        exittime.dwLowDateTime = 0;
        exittime.dwHighDateTime = 0;
        kerneltime.dwLowDateTime  = 0;
        kerneltime.dwHighDateTime  = 0;
        usertime.dwLowDateTime = 0;
        usertime.dwHighDateTime = 0;
   }
    memcpy(&li, &kerneltime, sizeof(FILETIME));
    li.QuadPart /= 10L;
    r->ru_stime.tv_sec = (long)(li.QuadPart / 1000000L);
    r->ru_stime.tv_usec = (long)(li.QuadPart % 1000000L);

    memcpy(&li, &usertime, sizeof(FILETIME));
    li.QuadPart /= 10L;
    r->ru_utime.tv_sec = (long)(li.QuadPart / 1000000L);
    r->ru_utime.tv_usec = (long)(li.QuadPart % 1000000L);

    return 0;
}

#define DELTA_EPOCH_IN_MICROSECS  11644473600000000Ui64

struct timezone
{
  int  tz_minuteswest; /* minutes W of Greenwich */
  int  tz_dsttime;     /* type of dst correction */
};

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
  FILETIME ft;
  unsigned __int64 tmpres = 0;
  static int tzflag;

  if (NULL != tv)
  {
    GetSystemTimeAsFileTime(&ft);

    tmpres |= ft.dwHighDateTime;
    tmpres <<= 32;
    tmpres |= ft.dwLowDateTime;

    /*converting file time to unix epoch*/
    tmpres -= DELTA_EPOCH_IN_MICROSECS; 
    tmpres /= 10;  /*convert into microseconds*/
    tv->tv_sec = (long)(tmpres / 1000000UL);
    tv->tv_usec = (long)(tmpres % 1000000UL);
  }

  if (NULL != tz)
  {
    if (!tzflag)
    {
      _tzset();
      tzflag++;
    }
    tz->tz_minuteswest = _timezone / 60;
    tz->tz_dsttime = _daylight;
  }

  return 0;
}

static _locale_t clocale = NULL;
double wstrtod(const char *nptr, char **eptr) {
    double d;
    char *leptr;
    if (clocale == NULL)
        clocale = _create_locale(LC_ALL, "C");
    d = _strtod_l(nptr, &leptr, clocale);
    /* if 0, check if input was inf */
    if (d == 0 && nptr == leptr) {
        int neg = 0;
        while (isspace(*nptr))
            nptr++;
        if (*nptr == '+')
            nptr++;
        else if (*nptr == '-') {
            nptr++;
            neg = 1;
        }

        if (strnicmp("INF", nptr, 3) == 0) {
            if (eptr != NULL) {
                if (strnicmp("INFINITE", nptr, 8) == 0)
                    *eptr = (char*)(nptr + 8);
                else
                    *eptr = (char*)(nptr + 3);
            }
            if (neg == 1)
                return -HUGE_VAL;
            else
                return HUGE_VAL;
        } else if (strnicmp("NAN", nptr, 3) == 0) {
            if (eptr != NULL)
                *eptr = (char*)(nptr + 3);
            /* create a NaN : 0 * infinity*/
            d = HUGE_VAL;
            return d * 0;
        }
    }
    if (eptr != NULL)
        *eptr = leptr;
    return d;
}

#endif
