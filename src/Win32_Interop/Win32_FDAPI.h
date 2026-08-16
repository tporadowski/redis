/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_INTEROP_FDAPI_H
#define WIN32_INTEROP_FDAPI_H

#ifdef _WIN32

#include <stddef.h>
#include "win32_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef socklen_t
typedef int socklen_t;
#endif
#ifndef nfds_t
typedef unsigned long nfds_t;
#endif
#ifndef sa_family_t
typedef unsigned short sa_family_t;
#endif

#ifndef AF_INET
#define AF_INET 2
#endif
#ifndef AF_INET6
#define AF_INET6 23
#endif
#ifndef AF_UNIX
#define AF_UNIX 1
#endif
#ifndef AF_LOCAL
#define AF_LOCAL AF_UNIX
#endif
#ifndef AF_UNSPEC
#define AF_UNSPEC 0
#endif
#ifndef SOCK_STREAM
#define SOCK_STREAM 1
#endif
#ifndef SOCK_DGRAM
#define SOCK_DGRAM 2
#endif
#ifndef SOL_SOCKET
#define SOL_SOCKET 0xffff
#endif
#ifndef SO_REUSEADDR
#define SO_REUSEADDR 0x0004
#endif
#ifndef SO_KEEPALIVE
#define SO_KEEPALIVE 0x0008
#endif
#ifndef SO_ERROR
#define SO_ERROR 0x1007
#endif
#ifndef SO_ACCEPTCONN
#define SO_ACCEPTCONN 0x0002
#endif
#ifndef SO_SNDBUF
#define SO_SNDBUF 0x1001
#endif
#ifndef SO_RCVBUF
#define SO_RCVBUF 0x1002
#endif
#ifndef SO_LINGER
#define SO_LINGER 0x0080
#endif
#ifndef SO_SNDTIMEO
#define SO_SNDTIMEO 0x1005
#endif
#ifndef SO_RCVTIMEO
#define SO_RCVTIMEO 0x1006
#endif
#ifndef IPPROTO_TCP
#define IPPROTO_TCP 6
#endif
#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6 41
#endif
#ifndef IPV6_V6ONLY
#define IPV6_V6ONLY 27
#endif
#ifndef AI_PASSIVE
#define AI_PASSIVE 0x01
#endif
#ifndef AI_NUMERICHOST
#define AI_NUMERICHOST 0x04
#endif
#ifndef EAI_FAIL
#define EAI_FAIL 4
#endif
#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif
#ifndef SHUT_RD
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2
#endif
#ifndef X_OK
#define X_OK 0
#define W_OK 2
#define R_OK 4
#endif
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
#ifndef POLLIN
#define POLLIN  0x0100
#define POLLPRI 0x0400
#define POLLOUT 0x0010
#define POLLERR 0x0001
#define POLLHUP 0x0002
#define POLLNVAL 0x0004
#endif

struct timeval;

#ifndef FDAPI_IMPLEMENTATION
struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};
struct in_addr {
    unsigned long s_addr;
};
struct sockaddr_in {
    sa_family_t sin_family;
    unsigned short sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};
struct in6_addr {
    unsigned char s6_addr[16];
};
struct sockaddr_in6 {
    sa_family_t sin6_family;
    unsigned short sin6_port;
    unsigned long sin6_flowinfo;
    struct in6_addr sin6_addr;
    unsigned long sin6_scope_id;
};
#ifndef _SS_MAXSIZE
#define _SS_MAXSIZE 128
struct sockaddr_storage {
    sa_family_t ss_family;
    char ss_pad[_SS_MAXSIZE - sizeof(sa_family_t)];
};
#endif
struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    size_t ai_addrlen;
    char *ai_canonname;
    struct sockaddr *ai_addr;
    struct addrinfo *ai_next;
};
#endif /* !FDAPI_IMPLEMENTATION */

struct redis_pollfd {
    int fd;
    short events;
    short revents;
};
#ifndef FDAPI_IMPLEMENTATION
#define pollfd redis_pollfd
#endif

#define REDIS_FD_SETSIZE 10240
typedef struct redis_fd_set {
    unsigned int fd_count;
    int fd_array[REDIS_FD_SETSIZE];
} redis_fd_set;

unsigned short fdapi_htons(unsigned short v);
unsigned short fdapi_ntohs(unsigned short v);
unsigned long fdapi_htonl(unsigned long v);
unsigned long fdapi_ntohl(unsigned long v);

void FDAPI_Init(void);
int fdapi_fd_isset(int fd, const redis_fd_set *set);

#ifndef FDAPI_IMPLEMENTATION
#ifndef FD_SETSIZE
#define FD_SETSIZE REDIS_FD_SETSIZE
#endif
#define fd_set redis_fd_set
#define FD_ZERO(s) do { (s)->fd_count = 0; } while (0)
#define FD_ISSET(fd, s) fdapi_fd_isset((fd), (s))
#define FD_SET(fd, s) do { \
    if (!FD_ISSET((fd), (s)) && (s)->fd_count < REDIS_FD_SETSIZE) \
        (s)->fd_array[(s)->fd_count++] = (int)(fd); \
} while (0)
#define FD_CLR(fd, s) do { \
    unsigned int _i; \
    for (_i = 0; _i < (s)->fd_count; ) { \
        if ((s)->fd_array[_i] == (int)(fd)) \
            (s)->fd_array[_i] = (s)->fd_array[--(s)->fd_count]; \
        else \
            _i++; \
    } \
} while (0)
#define htons fdapi_htons
#define ntohs fdapi_ntohs
#define htonl fdapi_htonl
#define ntohl fdapi_ntohl
#endif

int fdapi_socket(int af, int type, int protocol);
int fdapi_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int fdapi_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int fdapi_listen(int sockfd, int backlog);
int fdapi_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int fdapi_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int fdapi_getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int fdapi_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int fdapi_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int fdapi_shutdown(int sockfd, int how);

#ifndef FDAPI_IMPLEMENTATION
#define socket fdapi_socket
#define accept fdapi_accept
#define bind fdapi_bind
#define listen fdapi_listen
#define connect fdapi_connect
#define setsockopt fdapi_setsockopt
#define getsockopt fdapi_getsockopt
#define getpeername fdapi_getpeername
#define getsockname fdapi_getsockname
#define shutdown fdapi_shutdown
#endif

int fdapi_getaddrinfo(const char *node, const char *service,
                      const struct addrinfo *hints, struct addrinfo **res);
void fdapi_freeaddrinfo(struct addrinfo *ai);
const char *fdapi_gai_strerror(int errcode);
const char *fdapi_inet_ntop(int af, const void *src, char *dst, size_t size);
int fdapi_inet_pton(int af, const char *src, void *dst);

#ifndef FDAPI_IMPLEMENTATION
#define getaddrinfo fdapi_getaddrinfo
#define freeaddrinfo fdapi_freeaddrinfo
#define gai_strerror fdapi_gai_strerror
#define inet_ntop fdapi_inet_ntop
#define inet_pton fdapi_inet_pton
#endif

int pipe(int pipefd[2]);
int fsync(int fd);
int fcntl(int fd, int cmd, ...);
int fdapi_poll(struct redis_pollfd *fds, nfds_t nfds, int timeout);
int fdapi_select(int nfds, redis_fd_set *readfds, redis_fd_set *writefds,
                 redis_fd_set *exceptfds, struct timeval *timeout);
int ftruncate(int fd, off_t length);

#ifndef FDAPI_IMPLEMENTATION
#define poll fdapi_poll
#define select fdapi_select
#endif

int fdapi_close(int fd);
ssize_t fdapi_read(int fd, void *buf, size_t count);
ssize_t fdapi_write(int fd, const void *buf, size_t count);
int fdapi_isatty(int fd);
off_t fdapi_lseek(int fd, off_t offset, int whence);

typedef int (*fnWSIOCP_CloseSocketStateRFD)(int rfd);
void FDAPI_SetCloseSocketState(fnWSIOCP_CloseSocketStateRFD func);
int FDAPI_WSAGetLastError(void);
int FDAPI_SocketAttachIOCP(int rfd, void *iocph);
int FDAPI_WSASend(int rfd, void *buffers, unsigned long count,
                  unsigned long *sent, unsigned long flags,
                  void *overlapped, void *completion);
int FDAPI_WSARecv(int rfd, void *buffers, unsigned long count,
                  unsigned long *recvd, unsigned long *flags,
                  void *overlapped, void *completion);
int FDAPI_WSAIoctl(int rfd, unsigned long code, void *inbuf,
                   unsigned long inlen, void *outbuf, unsigned long outlen,
                   unsigned long *retlen, void *overlapped, void *completion);
void **FDAPI_GetSocketStatePtr(int rfd);
void FDAPI_ClearSocketInfo(int rfd);
intptr_t FDAPI_get_osfhandle(int fd);
int FDAPI_open_osfhandle(intptr_t osfhandle, int flags);
int FDAPI_AcceptEx(int listenfd, int acceptfd, void *buf,
                   unsigned long rxlen, unsigned long locallen,
                   unsigned long remotelen, unsigned long *recvd,
                   void *overlapped);
int FDAPI_ConnectEx(int rfd, const struct sockaddr *name, int namelen,
                    void *sendbuf, unsigned long sendlen,
                    unsigned long *sent, void *overlapped);
void FDAPI_GetAcceptExSockaddrs(int rfd, void *buf, unsigned long rxlen,
                                unsigned long locallen, unsigned long remotelen,
                                struct sockaddr **local, int *locallen_out,
                                struct sockaddr **remote, int *remotelen_out);
int FDAPI_UpdateAcceptContext(int acceptfd, int listenfd);
int FDAPI_WSAGetOverlappedResult(int rfd, void *overlapped,
                                 unsigned long *transferred, int wait,
                                 unsigned long *flags);

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */
#endif /* WIN32_INTEROP_FDAPI_H */
