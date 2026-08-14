/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/*
 * Redis fd layer for Windows: dense RFDs (0,1,2 reserved) over SOCKETs and CRT fds.
 */

#define FDAPI_IMPLEMENTATION
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef FD_SETSIZE
#define FD_SETSIZE 10240
#endif

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <mswsock.h>

#include "Win32_FDAPI.h"
#include "win32_rfdmap.h"
#include "Win32_fdapi_crt.h"
#include "Win32_Error.h"

#ifdef __cplusplus
extern "C" {
#endif
void InitTimeFunctions(void);
#ifdef __cplusplus
}
#endif

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdarg.h>
#include <stdlib.h>

#ifndef EAGAIN
#define EAGAIN 11
#endif
#ifndef EINPROGRESS
#define EINPROGRESS 115
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif
#ifndef SHUT_RD
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2
#endif

static INIT_ONCE g_wsa_once = INIT_ONCE_STATIC_INIT;
static fnWSIOCP_CloseSocketStateRFD g_close_sock_state;

static BOOL CALLBACK wsa_init_once(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    WSADATA wsa;
    (void)once;
    (void)param;
    (void)ctx;
    InitTimeFunctions();
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0 ? TRUE : FALSE;
}

void FDAPI_Init(void) {
    InitOnceExecuteOnce(&g_wsa_once, wsa_init_once, NULL, NULL);
}

static void set_wsa_errno(int connect_op) {
    int w = WSAGetLastError();
    if (connect_op && w == WSAEWOULDBLOCK) {
        errno = EINPROGRESS;
        return;
    }
    if (w == WSAEWOULDBLOCK) {
        errno = EAGAIN;
        return;
    }
    int e = translate_sys_error(w);
    errno = (e == -9999) ? EIO : e;
}

static SOCKET sock_of(int rfd) {
    return RFDMap::getInstance().lookupSocket(rfd);
}

int fdapi_fd_isset(int fd, const redis_fd_set *set) {
    unsigned i;
    if (!set) return 0;
    for (i = 0; i < set->fd_count; i++) {
        if (set->fd_array[i] == fd) return 1;
    }
    return 0;
}

unsigned short fdapi_htons(unsigned short v) {
    FDAPI_Init();
    return htons(v);
}
unsigned short fdapi_ntohs(unsigned short v) {
    FDAPI_Init();
    return ntohs(v);
}
unsigned long fdapi_htonl(unsigned long v) {
    FDAPI_Init();
    return htonl(v);
}
unsigned long fdapi_ntohl(unsigned long v) {
    FDAPI_Init();
    return ntohl(v);
}

int fdapi_socket(int af, int type, int protocol) {
    SOCKET s;
    RFD rfd;
    FDAPI_Init();
    s = WSASocket(af, type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (s == INVALID_SOCKET) {
        set_wsa_errno(0);
        return -1;
    }
    rfd = RFDMap::getInstance().addSocket(s);
    if (rfd == INVALID_FD) {
        closesocket(s);
        errno = EMFILE;
        return -1;
    }
    return rfd;
}

int fdapi_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    SOCKET s = sock_of(sockfd);
    SOCKET as;
    RFD rfd;
    int len = addrlen ? *addrlen : 0;
    if (s == INVALID_SOCKET) {
        errno = EBADF;
        return -1;
    }
    as = accept(s, addr, addrlen ? &len : NULL);
    if (as == INVALID_SOCKET) {
        set_wsa_errno(0);
        return -1;
    }
    if (addrlen) *addrlen = len;
    rfd = RFDMap::getInstance().addSocket(as);
    if (rfd == INVALID_FD) {
        closesocket(as);
        errno = EMFILE;
        return -1;
    }
    return rfd;
}

int fdapi_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    SOCKET s = sock_of(sockfd);
    if (s == INVALID_SOCKET) {
        errno = EBADF;
        return -1;
    }
    if (bind(s, addr, addrlen) == SOCKET_ERROR) {
        set_wsa_errno(0);
        return -1;
    }
    return 0;
}

int fdapi_listen(int sockfd, int backlog) {
    SOCKET s = sock_of(sockfd);
    if (s == INVALID_SOCKET) {
        errno = EBADF;
        return -1;
    }
    if (listen(s, backlog) == SOCKET_ERROR) {
        set_wsa_errno(0);
        return -1;
    }
    return 0;
}

int fdapi_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    SOCKET s = sock_of(sockfd);
    if (s == INVALID_SOCKET) {
        errno = EBADF;
        return -1;
    }
    if (connect(s, addr, (int)addrlen) == SOCKET_ERROR) {
        set_wsa_errno(1);
        return -1;
    }
    return 0;
}

int fdapi_setsockopt(int sockfd, int level, int optname, const void *optval,
                     socklen_t optlen) {
    SOCKET s = sock_of(sockfd);
    if (s == INVALID_SOCKET) {
        errno = EBADF;
        return -1;
    }
    if (level == SOL_SOCKET &&
        (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) &&
        optlen >= (socklen_t)sizeof(struct timeval)) {
        const struct timeval *tv = (const struct timeval *)optval;
        DWORD ms = (DWORD)(tv->tv_sec * 1000 + tv->tv_usec / 1000);
        if (setsockopt(s, level, optname, (const char *)&ms, sizeof(ms)) ==
            SOCKET_ERROR) {
            set_wsa_errno(0);
            return -1;
        }
        return 0;
    }
    if (setsockopt(s, level, optname, (const char *)optval, optlen) ==
        SOCKET_ERROR) {
        set_wsa_errno(0);
        return -1;
    }
    return 0;
}

int fdapi_getsockopt(int sockfd, int level, int optname, void *optval,
                     socklen_t *optlen) {
    SOCKET s = sock_of(sockfd);
    int len;
    if (s == INVALID_SOCKET) {
        errno = EBADF;
        return -1;
    }
    len = optlen ? *optlen : 0;
    if (getsockopt(s, level, optname, (char *)optval, &len) == SOCKET_ERROR) {
        set_wsa_errno(0);
        return -1;
    }
    if (optlen) *optlen = len;
    if (level == SOL_SOCKET && optname == SO_ERROR && optval) {
        int err = *(int *)optval;
        if (err) *(int *)optval = translate_sys_error(err);
    }
    return 0;
}

int fdapi_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    SOCKET s = sock_of(sockfd);
    int len = addrlen ? *addrlen : 0;
    if (s == INVALID_SOCKET) {
        errno = EBADF;
        return -1;
    }
    if (getpeername(s, addr, &len) == SOCKET_ERROR) {
        set_wsa_errno(0);
        return -1;
    }
    if (addrlen) *addrlen = len;
    return 0;
}

int fdapi_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    SOCKET s = sock_of(sockfd);
    int len = addrlen ? *addrlen : 0;
    if (s == INVALID_SOCKET) {
        errno = EBADF;
        return -1;
    }
    if (getsockname(s, addr, &len) == SOCKET_ERROR) {
        set_wsa_errno(0);
        return -1;
    }
    if (addrlen) *addrlen = len;
    return 0;
}

int fdapi_shutdown(int sockfd, int how) {
    SOCKET s = sock_of(sockfd);
    int w = SD_BOTH;
    if (s == INVALID_SOCKET) {
        errno = EBADF;
        return -1;
    }
    if (how == SHUT_RD) w = SD_RECEIVE;
    else if (how == SHUT_WR) w = SD_SEND;
    if (shutdown(s, w) == SOCKET_ERROR) {
        set_wsa_errno(0);
        return -1;
    }
    return 0;
}

int fdapi_getaddrinfo(const char *node, const char *service,
                      const struct addrinfo *hints, struct addrinfo **res) {
    int rc;
    FDAPI_Init();
    rc = getaddrinfo(node, service, hints, res);
    if (rc != 0) {
        set_wsa_errno(0);
        return rc;
    }
    return 0;
}

void fdapi_freeaddrinfo(struct addrinfo *ai) {
    if (ai) freeaddrinfo(ai);
}

const char *fdapi_gai_strerror(int errcode) {
    return gai_strerrorA(errcode);
}

const char *fdapi_inet_ntop(int af, const void *src, char *dst, size_t size) {
    FDAPI_Init();
    return inet_ntop(af, src, dst, size);
}

int fdapi_inet_pton(int af, const char *src, void *dst) {
    FDAPI_Init();
    return inet_pton(af, src, dst);
}

int pipe(int pipefd[2]) {
    int crt[2];
    if (crt_pipe(crt, 4096, _O_BINARY) != 0)
        return -1;
    pipefd[0] = RFDMap::getInstance().addCrtFD(crt[0]);
    pipefd[1] = RFDMap::getInstance().addCrtFD(crt[1]);
    if (pipefd[0] < 0 || pipefd[1] < 0) {
        crt_close(crt[0]);
        crt_close(crt[1]);
        errno = EMFILE;
        return -1;
    }
    return 0;
}

int fsync(int fd) {
    int crt = RFDMap::getInstance().lookupCrtFD(fd);
    if (crt < 0) {
        errno = EBADF;
        return -1;
    }
    return crt_commit(crt);
}

int fcntl(int fd, int cmd, ...) {
    SocketInfo *info = RFDMap::getInstance().lookupSocketInfo(fd);
    va_list ap;
    int flags = 0;
    if (cmd == F_GETFL) {
        return info ? info->flags : 0;
    }
    if (cmd == F_SETFL) {
        va_start(ap, cmd);
        flags = va_arg(ap, int);
        va_end(ap);
        if (info) {
            SOCKET s = info->socket;
            u_long nb = (flags & O_NONBLOCK) ? 1 : 0;
            if (ioctlsocket(s, FIONBIO, &nb) == SOCKET_ERROR) {
                set_wsa_errno(0);
                return -1;
            }
            info->flags = flags;
        }
        return 0;
    }
    if (cmd == F_SETFD) {
        va_start(ap, cmd);
        flags = va_arg(ap, int);
        va_end(ap);
        {
            SOCKET s = sock_of(fd);
            HANDLE h = NULL;
            if (s != INVALID_SOCKET)
                h = (HANDLE)s;
            else {
                int crt = RFDMap::getInstance().lookupCrtFD(fd);
                if (crt >= 0) h = (HANDLE)crt_get_osfhandle(crt);
            }
            if (h && h != INVALID_HANDLE_VALUE) {
                SetHandleInformation(h, HANDLE_FLAG_INHERIT,
                                     (flags & FD_CLOEXEC) ? 0 : HANDLE_FLAG_INHERIT);
            }
        }
        return 0;
    }
    if (cmd == F_GETFD)
        return 0;
    errno = EINVAL;
    return -1;
}

int fdapi_poll(struct redis_pollfd *fds, nfds_t nfds, int timeout) {
    WSAPOLLFD stack[64];
    WSAPOLLFD *ws;
    nfds_t i;
    int rc;
    FDAPI_Init();
    if (nfds == 0) return 0;
    ws = (nfds <= 64) ? stack : (WSAPOLLFD *)malloc(sizeof(WSAPOLLFD) * nfds);
    if (!ws) {
        errno = ENOMEM;
        return -1;
    }
    for (i = 0; i < nfds; i++) {
        SOCKET s = sock_of(fds[i].fd);
        ws[i].fd = (s == INVALID_SOCKET) ? INVALID_SOCKET : s;
        ws[i].events = fds[i].events;
        ws[i].revents = 0;
    }
    rc = WSAPoll(ws, (ULONG)nfds, timeout);
    if (rc == SOCKET_ERROR) {
        set_wsa_errno(0);
        if (ws != stack) free(ws);
        return -1;
    }
    for (i = 0; i < nfds; i++)
        fds[i].revents = ws[i].revents;
    if (ws != stack) free(ws);
    return rc;
}

static void rfdset_to_ws(const redis_fd_set *in, fd_set *out) {
    unsigned i;
    FD_ZERO(out);
    if (!in) return;
    for (i = 0; i < in->fd_count; i++) {
        SOCKET s = sock_of(in->fd_array[i]);
        if (s != INVALID_SOCKET) FD_SET(s, out);
    }
}

static void ws_to_rfdset(const fd_set *in, redis_fd_set *out) {
    u_int i;
    out->fd_count = 0;
    for (i = 0; i < in->fd_count; i++) {
        RFD r = RFDMap::getInstance().socketToRFD(in->fd_array[i]);
        if (r != INVALID_FD && out->fd_count < REDIS_FD_SETSIZE)
            out->fd_array[out->fd_count++] = r;
    }
}

int fdapi_select(int nfds, redis_fd_set *readfds, redis_fd_set *writefds,
                 redis_fd_set *exceptfds, struct timeval *timeout) {
    fd_set rs, ws, es;
    int rc;
    FDAPI_Init();
    rfdset_to_ws(readfds, &rs);
    rfdset_to_ws(writefds, &ws);
    rfdset_to_ws(exceptfds, &es);
    rc = ::select(nfds, readfds ? &rs : NULL, writefds ? &ws : NULL,
                  exceptfds ? &es : NULL, timeout);
    if (rc == SOCKET_ERROR) {
        set_wsa_errno(0);
        return -1;
    }
    if (readfds) ws_to_rfdset(&rs, readfds);
    if (writefds) ws_to_rfdset(&ws, writefds);
    if (exceptfds) ws_to_rfdset(&es, exceptfds);
    return rc;
}

int ftruncate(int fd, off_t length) {
    int crt = RFDMap::getInstance().lookupCrtFD(fd);
    if (crt < 0) {
        errno = EBADF;
        return -1;
    }
    return crt_chsize64(crt, length);
}

int fdapi_close(int fd) {
    SOCKET s = sock_of(fd);
    if (s != INVALID_SOCKET) {
        if (g_close_sock_state)
            g_close_sock_state(fd);
        RFDMap::getInstance().removeRFDToSocketInfo(fd);
        RFDMap::getInstance().removeSocketToRFD(s);
        if (closesocket(s) == SOCKET_ERROR) {
            set_wsa_errno(0);
            return -1;
        }
        return 0;
    }
    {
        int crt = RFDMap::getInstance().lookupCrtFD(fd);
        if (crt < 0) {
            errno = EBADF;
            return -1;
        }
        RFDMap::getInstance().removeCrtFD(crt);
        return crt_close(crt);
    }
}

ssize_t fdapi_read(int fd, void *buf, size_t count) {
    SOCKET s = sock_of(fd);
    if (s != INVALID_SOCKET) {
        int n = recv(s, (char *)buf, (int)count, 0);
        if (n == SOCKET_ERROR) {
            set_wsa_errno(0);
            return -1;
        }
        return n;
    }
    {
        int crt = RFDMap::getInstance().lookupCrtFD(fd);
        int n;
        if (crt < 0) {
            errno = EBADF;
            return -1;
        }
        n = crt_read(crt, buf, (unsigned)count);
        return n;
    }
}

ssize_t fdapi_write(int fd, const void *buf, size_t count) {
    SOCKET s = sock_of(fd);
    if (s != INVALID_SOCKET) {
        int n = send(s, (const char *)buf, (int)count, 0);
        if (n == SOCKET_ERROR) {
            set_wsa_errno(0);
            return -1;
        }
        return n;
    }
    {
        int crt = RFDMap::getInstance().lookupCrtFD(fd);
        int n;
        if (crt < 0) {
            errno = EBADF;
            return -1;
        }
        n = crt_write(crt, buf, (unsigned)count);
        return n;
    }
}

int fdapi_isatty(int fd) {
    int crt = RFDMap::getInstance().lookupCrtFD(fd);
    if (crt < 0) return 0;
    return crt_isatty(crt);
}

off_t fdapi_lseek(int fd, off_t offset, int whence) {
    int crt = RFDMap::getInstance().lookupCrtFD(fd);
    if (crt < 0) {
        errno = EBADF;
        return -1;
    }
    return (off_t)crt_lseek64(crt, offset, whence);
}

void FDAPI_SetCloseSocketState(fnWSIOCP_CloseSocketStateRFD func) {
    g_close_sock_state = func;
}

int FDAPI_WSAGetLastError(void) { return WSAGetLastError(); }

int FDAPI_SocketAttachIOCP(int rfd, void *iocph) {
    SOCKET s = sock_of(rfd);
    if (s == INVALID_SOCKET || !iocph) return 0;
    return CreateIoCompletionPort((HANDLE)s, (HANDLE)iocph,
                                  (ULONG_PTR)(intptr_t)rfd, 0) != NULL;
}

int FDAPI_WSASend(int rfd, void *buffers, unsigned long count,
                  unsigned long *sent, unsigned long flags, void *overlapped,
                  void *completion) {
    SOCKET s = sock_of(rfd);
    if (s == INVALID_SOCKET) {
        errno = EBADF;
        return SOCKET_ERROR;
    }
    return WSASend(s, (LPWSABUF)buffers, count, sent, flags,
                   (LPWSAOVERLAPPED)overlapped,
                   (LPWSAOVERLAPPED_COMPLETION_ROUTINE)completion);
}

int FDAPI_WSARecv(int rfd, void *buffers, unsigned long count,
                  unsigned long *recvd, unsigned long *flags, void *overlapped,
                  void *completion) {
    SOCKET s = sock_of(rfd);
    if (s == INVALID_SOCKET) {
        errno = EBADF;
        return SOCKET_ERROR;
    }
    return WSARecv(s, (LPWSABUF)buffers, count, recvd, flags,
                   (LPWSAOVERLAPPED)overlapped,
                   (LPWSAOVERLAPPED_COMPLETION_ROUTINE)completion);
}

int FDAPI_WSAIoctl(int rfd, unsigned long code, void *inbuf, unsigned long inlen,
                   void *outbuf, unsigned long outlen, unsigned long *retlen,
                   void *overlapped, void *completion) {
    SOCKET s = sock_of(rfd);
    if (s == INVALID_SOCKET) {
        errno = EBADF;
        return SOCKET_ERROR;
    }
    return WSAIoctl(s, code, inbuf, inlen, outbuf, outlen, retlen,
                    (LPWSAOVERLAPPED)overlapped,
                    (LPWSAOVERLAPPED_COMPLETION_ROUTINE)completion);
}

void **FDAPI_GetSocketStatePtr(int rfd) {
    SocketInfo *info = RFDMap::getInstance().lookupSocketInfo(rfd);
    return info ? &info->state : NULL;
}

void FDAPI_ClearSocketInfo(int rfd) {
    SOCKET s = sock_of(rfd);
    if (s != INVALID_SOCKET) {
        RFDMap::getInstance().removeRFDToSocketInfo(rfd);
        RFDMap::getInstance().removeSocketToRFD(s);
    }
}

intptr_t FDAPI_get_osfhandle(int fd) {
    int crt = RFDMap::getInstance().lookupCrtFD(fd);
    if (crt < 0) return -1;
    return crt_get_osfhandle(crt);
}

int FDAPI_open_osfhandle(intptr_t osfhandle, int flags) {
    int crt = crt_open_osfhandle(osfhandle, flags);
    if (crt < 0) return -1;
    return RFDMap::getInstance().addCrtFD(crt);
}
