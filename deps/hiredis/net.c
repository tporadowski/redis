/* Extracted from anet.c to work properly with Hiredis error reporting.
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "fmacros.h"
#include <sys/types.h>
#ifdef _WIN32
  #ifndef FD_SETSIZE
    #define FD_SETSIZE 16000
  #endif
  #include "winsock2.h"
  #include "windows.h"
  #define socklen_t int
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#endif
#include <fcntl.h>
#include <string.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

#include "net.h"
#include "sds.h"

/* Forward declaration */
void __redisSetError(redisContext *c, int type, sds err);

#ifdef _WIN32
static int redisCreateSocket(redisContext *c, int type) {
    SOCKET s;
    int on=1;

    s = socket(type, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        __redisSetError(c,REDIS_ERR_IO,sdscatprintf(sdsempty(), "socket error: %d\n", WSAGetLastError()));
        return REDIS_ERR;
    }
    if (type == AF_INET) {
        LINGER l;
        l.l_onoff = 1;
        l.l_linger = 2;
        setsockopt(s, SOL_SOCKET, SO_LINGER, (const char *) &l, sizeof(l));

        if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *) &on, sizeof(on)) == -1) {
            __redisSetError(c,REDIS_ERR_IO,NULL);
            closesocket(s);
            return REDIS_ERR;
        }
    }
    return (int)s;
}
#else
static int redisCreateSocket(redisContext *c, int type) {
    int s, on = 1;
    if ((s = socket(type, SOCK_STREAM, 0)) == -1) {
        __redisSetError(c,REDIS_ERR_IO,NULL);
        return REDIS_ERR;
    }
    if (type == AF_INET) {
        if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
            __redisSetError(c,REDIS_ERR_IO,NULL);
            close(s);
            return REDIS_ERR;
        }
    }
    return s;
}
#endif


#ifdef _WIN32
static int redisSetBlocking(redisContext *c, int fd, int blocking) {
    /* If iMode = 0, blocking is enabled; */
    /* If iMode != 0, non-blocking mode is enabled. */
    u_long flags;

    if (blocking)
        flags = (u_long)0;
    else
        flags = (u_long)1;

    if (ioctlsocket((SOCKET)fd, FIONBIO, &flags) == SOCKET_ERROR) {
        errno = WSAGetLastError();
        __redisSetError(c,REDIS_ERR_IO,
            sdscatprintf(sdsempty(), "ioctlsocket(FIONBIO): %d\n", errno));
        closesocket(fd);
        return REDIS_ERR;
    };
    return REDIS_OK;
}

#else
static int redisSetBlocking(redisContext *c, int fd, int blocking) {
    int flags;

    /* Set the socket nonblocking.
     * Note that fcntl(2) for F_GETFL and F_SETFL can't be
     * interrupted by a signal. */
    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        __redisSetError(c,REDIS_ERR_IO,
            sdscatprintf(sdsempty(), "fcntl(F_GETFL): %s", strerror(errno)));
        close(fd);
        return REDIS_ERR;
    }

    if (blocking)
        flags &= ~O_NONBLOCK;
    else
        flags |= O_NONBLOCK;

    if (fcntl(fd, F_SETFL, flags) == -1) {
        __redisSetError(c,REDIS_ERR_IO,
            sdscatprintf(sdsempty(), "fcntl(F_SETFL): %s", strerror(errno)));
        close(fd);
        return REDIS_ERR;
    }
    return REDIS_OK;
}
#endif

#ifdef _WIN32
static int redisSetTcpNoDelay(redisContext *c, int fd) {
    int yes = 1;
    if (setsockopt((SOCKET)fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, sizeof(yes)) == -1) {
        __redisSetError(c,REDIS_ERR_IO,
            sdscatprintf(sdsempty(), "setsockopt(TCP_NODELAY): %d", (int)GetLastError()));
        closesocket(fd);
        return REDIS_ERR;
    }
    return REDIS_OK;
}
#else
static int redisSetTcpNoDelay(redisContext *c, int fd) {
    int yes = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == -1) {
        __redisSetError(c,REDIS_ERR_IO,
            sdscatprintf(sdsempty(), "setsockopt(TCP_NODELAY): %s", strerror(errno)));
        close(fd);
        return REDIS_ERR;
    }
    return REDIS_OK;
}
#endif

static int redisContextWaitReady(redisContext *c, int fd, const struct timeval *timeout) {
    struct timeval to;
    struct timeval *toptr = NULL;
    fd_set wfd;
    int err;
    socklen_t errlen;

    /* Only use timeout when not NULL. */
    if (timeout != NULL) {
        to = *timeout;
        toptr = &to;
    }

    if (errno == EINPROGRESS) {
        FD_ZERO(&wfd);
#ifdef _WIN32
        FD_SET((SOCKET)fd, &wfd);
#else
        FD_SET(fd, &wfd);
#endif

        if (select(FD_SETSIZE, NULL, &wfd, NULL, toptr) == -1) {
            __redisSetError(c,REDIS_ERR_IO,
                sdscatprintf(sdsempty(), "select(2): %s", strerror(errno)));
#ifdef _WIN32
            closesocket(fd);
#else
            close(fd);
#endif
            return REDIS_ERR;
        }

        if (!FD_ISSET(fd, &wfd)) {
#ifdef _WIN32
            errno = WSAGetLastError();
            __redisSetError(c,REDIS_ERR_IO,NULL);
            closesocket(fd);
#else
            errno = ETIMEDOUT;
            __redisSetError(c,REDIS_ERR_IO,NULL);
            close(fd);
#endif
            return REDIS_ERR;
        }

        err = 0;
        errlen = sizeof(err);
#ifdef _WIN32
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&err, &errlen) == SOCKET_ERROR) {
            __redisSetError(c,REDIS_ERR_IO,
                sdscatprintf(sdsempty(), "getsockopt(SO_ERROR): %d", WSAGetLastError()));
            closesocket(fd);
            return REDIS_ERR;
        }
#else
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen) == -1) {
            __redisSetError(c,REDIS_ERR_IO,
                sdscatprintf(sdsempty(), "getsockopt(SO_ERROR): %s", strerror(errno)));
            close(fd);
            return REDIS_ERR;
        }
#endif

        if (err) {
            errno = err;
            __redisSetError(c,REDIS_ERR_IO,NULL);
#ifdef _WIN32
            closesocket(fd);
#else
            close(fd);
#endif
            return REDIS_ERR;
        }

        return REDIS_OK;
    }

    __redisSetError(c,REDIS_ERR_IO,NULL);
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
    return REDIS_ERR;
}

#ifdef _WIN32
int redisContextSetTimeout(redisContext *c, struct timeval tv) {
    if (setsockopt(c->fd,SOL_SOCKET,SO_RCVTIMEO,(const char *)&tv,sizeof(tv)) == SOCKET_ERROR ) {
        __redisSetError(c,REDIS_ERR_IO,
            sdscatprintf(sdsempty(), "setsockopt(SO_RCVTIMEO): %d",  WSAGetLastError()));
        return REDIS_ERR;
    }
    if (setsockopt(c->fd,SOL_SOCKET,SO_SNDTIMEO,(const char *)&tv,sizeof(tv)) == SOCKET_ERROR ) {
        __redisSetError(c,REDIS_ERR_IO,
            sdscatprintf(sdsempty(), "setsockopt(SO_SNDTIMEO): %d",  WSAGetLastError()));
        return REDIS_ERR;
    }
    return REDIS_OK;
}
#else
int redisContextSetTimeout(redisContext *c, struct timeval tv) {
    if (setsockopt(c->fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv)) == -1) {
        __redisSetError(c,REDIS_ERR_IO,
            sdscatprintf(sdsempty(), "setsockopt(SO_RCVTIMEO): %s", strerror(errno)));
        return REDIS_ERR;
    }
    if (setsockopt(c->fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv)) == -1) {
        __redisSetError(c,REDIS_ERR_IO,
            sdscatprintf(sdsempty(), "setsockopt(SO_SNDTIMEO): %s", strerror(errno)));
        return REDIS_ERR;
    }
    return REDIS_OK;
}
#endif

#ifdef _WIN32
int redisContextConnectTcp(redisContext *c, const char *addr, int port, struct timeval *timeout) {
    int s;
    int blocking = (c->flags & REDIS_BLOCK);
    struct sockaddr_in sa;
    unsigned long inAddress;

    if ((s = redisCreateSocket(c,AF_INET)) < 0)
        return REDIS_ERR;

    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (redisSetTcpNoDelay(c,s) != REDIS_OK)
        return REDIS_ERR;

    inAddress = inet_addr(addr);
    if (inAddress == INADDR_NONE || inAddress == INADDR_ANY) {
        struct hostent *he;

        he = gethostbyname(addr);
        if (he == NULL) {
            __redisSetError(c,REDIS_ERR_OTHER,
                sdscatprintf(sdsempty(),"can't resolve: %s\n", addr));
            closesocket(s);
            return REDIS_ERR;;
        }
        memcpy(&sa.sin_addr, he->h_addr, sizeof(struct in_addr));
    }
    else {
        sa.sin_addr.s_addr = inAddress;
    }

    if (connect((SOCKET)s, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
        errno = WSAGetLastError();
        if ((errno == WSAEINVAL) || (errno == WSAEWOULDBLOCK))
            errno = EINPROGRESS;
        if (errno == EINPROGRESS && !blocking) {
            /* This is ok. */
        } else {
            if (redisContextWaitReady(c,s,timeout) != REDIS_OK)
                return REDIS_ERR;
        }
    }

    c->fd = s;
    c->flags |= REDIS_CONNECTED;
    return REDIS_OK;
}
#else
int redisContextConnectTcp(redisContext *c, const char *addr, int port, struct timeval *timeout) {
    int s;
    int blocking = (c->flags & REDIS_BLOCK);
    struct sockaddr_in sa;

    if ((s = redisCreateSocket(c,AF_INET)) < 0)
        return REDIS_ERR;
    if (redisSetBlocking(c,s,0) != REDIS_OK)
        return REDIS_ERR;

    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (inet_aton(addr, &sa.sin_addr) == 0) {
        struct hostent *he;

        he = gethostbyname(addr);
        if (he == NULL) {
            __redisSetError(c,REDIS_ERR_OTHER,
                sdscatprintf(sdsempty(),"Can't resolve: %s",addr));
            close(s);
            return REDIS_ERR;
        }
        memcpy(&sa.sin_addr, he->h_addr, sizeof(struct in_addr));
    }

    if (connect(s, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
        if (errno == EINPROGRESS && !blocking) {
            /* This is ok. */
        } else {
            if (redisContextWaitReady(c,s,timeout) != REDIS_OK)
                return REDIS_ERR;
        }
    }

    /* Reset socket to be blocking after connect(2). */
    if (blocking && redisSetBlocking(c,s,1) != REDIS_OK)
        return REDIS_ERR;

    if (redisSetTcpNoDelay(c,s) != REDIS_OK)
        return REDIS_ERR;

    c->fd = s;
    c->flags |= REDIS_CONNECTED;
    return REDIS_OK;
}
#endif

int redisContextConnectUnix(redisContext *c, const char *path, struct timeval *timeout) {
#ifdef _WIN32
    (void) timeout;
    __redisSetError(c,REDIS_ERR_IO,
        sdscatprintf(sdsempty(),"Unix sockets are not suported on Windows platform. (%s)\n", path));

    return REDIS_ERR;
#else
    int s;
    int blocking = (c->flags & REDIS_BLOCK);
    struct sockaddr_un sa;

    if ((s = redisCreateSocket(c,AF_LOCAL)) < 0)
        return REDIS_ERR;
    if (redisSetBlocking(c,s,0) != REDIS_OK)
        return REDIS_ERR;

    sa.sun_family = AF_LOCAL;
    strncpy(sa.sun_path,path,sizeof(sa.sun_path)-1);
    if (connect(s, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
        if (errno == EINPROGRESS && !blocking) {
            /* This is ok. */
        } else {
            if (redisContextWaitReady(c,s,timeout) != REDIS_OK)
                return REDIS_ERR;
        }
    }

    /* Reset socket to be blocking after connect(2). */
    if (blocking && redisSetBlocking(c,s,1) != REDIS_OK)
        return REDIS_ERR;

    c->fd = s;
    c->flags |= REDIS_CONNECTED;
    return REDIS_OK;
#endif
}
