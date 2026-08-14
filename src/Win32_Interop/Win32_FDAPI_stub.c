/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#include "Win32_FDAPI.h"
#include "Win32_Time.h"
#include <io.h>
#include <errno.h>
#include <stdlib.h>

#ifndef UNUSED
#define UNUSED(V) ((void)(V))
#endif

void FDAPI_Init(void) {
    InitTimeFunctions();
}

static int nosys(void) {
    errno = ENOSYS;
    return -1;
}

int fdapi_fd_isset(int fd, const fd_set *set) {
    unsigned i;
    if (!set) return 0;
    for (i = 0; i < set->fd_count; i++) {
        if (set->fd_array[i] == fd) return 1;
    }
    return 0;
}

unsigned short htons(unsigned short v) {
    return (unsigned short)((v << 8) | (v >> 8));
}
unsigned short ntohs(unsigned short v) { return htons(v); }
unsigned long htonl(unsigned long v) {
    return ((v & 0xff000000ul) >> 24) | ((v & 0x00ff0000ul) >> 8) |
           ((v & 0x0000ff00ul) << 8) | ((v & 0x000000fful) << 24);
}
unsigned long ntohl(unsigned long v) { return htonl(v); }

int fdapi_socket(int af, int type, int protocol) {
    UNUSED(af); UNUSED(type); UNUSED(protocol);
    return nosys();
}
int fdapi_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    UNUSED(sockfd); UNUSED(addr); UNUSED(addrlen);
    return nosys();
}
int fdapi_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    UNUSED(sockfd); UNUSED(addr); UNUSED(addrlen);
    return nosys();
}
int fdapi_listen(int sockfd, int backlog) {
    UNUSED(sockfd); UNUSED(backlog);
    return nosys();
}
int fdapi_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    UNUSED(sockfd); UNUSED(addr); UNUSED(addrlen);
    return nosys();
}
int fdapi_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
    UNUSED(sockfd); UNUSED(level); UNUSED(optname); UNUSED(optval); UNUSED(optlen);
    return nosys();
}
int fdapi_getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen) {
    UNUSED(sockfd); UNUSED(level); UNUSED(optname); UNUSED(optval); UNUSED(optlen);
    return nosys();
}
int fdapi_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    UNUSED(sockfd); UNUSED(addr); UNUSED(addrlen);
    return nosys();
}
int fdapi_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    UNUSED(sockfd); UNUSED(addr); UNUSED(addrlen);
    return nosys();
}
int fdapi_shutdown(int sockfd, int how) {
    UNUSED(sockfd); UNUSED(how);
    return nosys();
}

int fdapi_getaddrinfo(const char *node, const char *service,
                      const struct addrinfo *hints, struct addrinfo **res) {
    UNUSED(node); UNUSED(service); UNUSED(hints); UNUSED(res);
    return EAI_FAIL;
}
void fdapi_freeaddrinfo(struct addrinfo *ai) { UNUSED(ai); }
const char *fdapi_gai_strerror(int errcode) {
    UNUSED(errcode);
    return "getaddrinfo not implemented (0.2 stub)";
}
const char *fdapi_inet_ntop(int af, const void *src, char *dst, size_t size) {
    UNUSED(af); UNUSED(src); UNUSED(dst); UNUSED(size);
    return NULL;
}
int fdapi_inet_pton(int af, const char *src, void *dst) {
    UNUSED(af); UNUSED(src); UNUSED(dst);
    return nosys();
}

int pipe(int pipefd[2]) {
    return _pipe(pipefd, 4096, 0x8000); /* _O_BINARY */
}
int fsync(int fd) { return _commit(fd); }
int fcntl(int fd, int cmd, ...) {
    UNUSED(fd);
    UNUSED(cmd);
    return 0;
}
int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    UNUSED(fds); UNUSED(nfds); UNUSED(timeout);
    return nosys();
}
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout) {
    UNUSED(nfds); UNUSED(readfds); UNUSED(writefds); UNUSED(exceptfds); UNUSED(timeout);
    return nosys();
}
int ftruncate(int fd, off_t length) {
    return _chsize_s(fd, length) == 0 ? 0 : -1;
}
