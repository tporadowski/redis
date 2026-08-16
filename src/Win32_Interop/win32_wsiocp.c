/*
 * Copyright (c), Microsoft Open Technologies, Inc.
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define FDAPI_IMPLEMENTATION
#include "win32_winsock.h"
#include "win32_wsiocp.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifndef EINVAL
#define EINVAL 22
#endif

#define MAX_COMPLETE_PER_POLL 100
#define ACCEPTEX_ADDR_BUF (sizeof(struct sockaddr_storage) + 32)
#define SUCCEEDED_WITH_IOCP(ok) ((ok) || (GetLastError() == ERROR_IO_PENDING))

typedef struct asendreq {
    OVERLAPPED ov;
    WSABUF wbuf;
    WSIOCP_Request req;
    aeFileProc *proc;
    aeEventLoop *eventLoop;
    struct asendreq *next;
} asendreq;

typedef struct aacceptreq {
    OVERLAPPED ov;
    int accept;
    void *buf;
    struct aacceptreq *next;
} aacceptreq;

typedef struct iocpSockState {
    int masks;
    int fd;
    void *iocp;
    aacceptreq *reqs;
    int wreqs;
    OVERLAPPED ov_read;
    asendreq *wreqlist;
    int unknownComplete;
} iocpSockState;

static char zreadchar[1];
static int close_hook_set;

static void *walloc(size_t n) { return calloc(1, n); }
static void wfree(void *p) { free(p); }

static int unlink_wreq(iocpSockState *ss, asendreq *areq) {
    asendreq **pp = &ss->wreqlist;
    while (*pp) {
        if (*pp == areq) {
            *pp = areq->next;
            return 1;
        }
        pp = &(*pp)->next;
    }
    return 0;
}

iocpSockState *WSIOCP_GetExistingSocketState(int fd) {
    iocpSockState **slot = (iocpSockState **)FDAPI_GetSocketStatePtr(fd);
    if (!slot) return NULL;
    return *slot;
}

static iocpSockState *WSIOCP_GetSocketState(int fd) {
    iocpSockState **slot = (iocpSockState **)FDAPI_GetSocketStatePtr(fd);
    if (!slot) return NULL;
    if (*slot == NULL) {
        *slot = (iocpSockState *)walloc(sizeof(iocpSockState));
        if (*slot) (*slot)->fd = fd;
    }
    return *slot;
}

static int WSIOCP_CloseSocketState(iocpSockState *ss) {
    if (!ss) return 1;
    ss->masks &= ~(SOCKET_ATTACHED | AE_WRITABLE | AE_READABLE);
    if (ss->wreqs == 0 &&
        (ss->masks & (READ_QUEUED | CONNECT_PENDING)) == 0) {
        wfree(ss);
        return 1;
    }
    ss->masks |= CLOSE_PENDING;
    return 0;
}

int WSIOCP_CloseSocketStateRFD(int rfd) {
    return WSIOCP_CloseSocketState(WSIOCP_GetExistingSocketState(rfd));
}

void *WSIOCP_CreateIocp(void) {
    HANDLE h = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    return h == NULL ? NULL : (void *)h;
}

void WSIOCP_CloseIocp(void *iocp) {
    if (iocp) CloseHandle((HANDLE)iocp);
}

void WSIOCP_OnLoopCreated(void) {
    if (!close_hook_set) {
        FDAPI_SetCloseSocketState(WSIOCP_CloseSocketStateRFD);
        close_hook_set = 1;
    }
}

void *WSIOCP_GetLoopIocp(aeEventLoop *el) {
    aeApiState *state;
    if (!el || !el->apidata) return NULL;
    state = (aeApiState *)el->apidata;
    return state->iocp;
}

void *WSIOCP_GetAssociatedIocp(int fd) {
    iocpSockState *ss = WSIOCP_GetExistingSocketState(fd);
    return ss ? ss->iocp : NULL;
}

int WSIOCP_Associate(int fd, void *iocp) {
    iocpSockState *ss = WSIOCP_GetSocketState(fd);
    if (!ss || !iocp) {
        errno = EINVAL;
        return -1;
    }
    if (ss->iocp == iocp)
        return 0;
    if (ss->iocp != NULL)
        return 1;
    if (!FDAPI_SocketAttachIOCP(fd, iocp)) {
        errno = EINVAL;
        return -1;
    }
    ss->iocp = iocp;
    ss->masks |= SOCKET_ATTACHED;
    ss->wreqs = 0;
    return 0;
}

int WSIOCP_QueueNextRead(int fd) {
    iocpSockState *ss;
    WSABUF zbuf;
    unsigned long recvd = 0;
    unsigned long flags = 0;
    int rc;

    ss = WSIOCP_GetSocketState(fd);
    if (!ss) {
        errno = EINVAL;
        return -1;
    }
    if ((ss->masks & SOCKET_ATTACHED) == 0)
        return 0;

    memset(&ss->ov_read, 0, sizeof(ss->ov_read));
    zbuf.buf = zreadchar;
    zbuf.len = 0;
    rc = FDAPI_WSARecv(fd, &zbuf, 1, &recvd, &flags, &ss->ov_read, NULL);
    if (SUCCEEDED_WITH_IOCP(rc == 0)) {
        ss->masks |= READ_QUEUED;
        return 0;
    }
    errno = FDAPI_WSAGetLastError();
    ss->masks &= ~READ_QUEUED;
    return -1;
}

int WSIOCP_QueueAccept(int listenfd) {
    iocpSockState *lss, *ass;
    aacceptreq *areq;
    struct sockaddr_storage name;
    socklen_t namelen = sizeof(name);
    int acceptfd, family = AF_INET;
    unsigned long bytes = 0;
    int rc;

    lss = WSIOCP_GetSocketState(listenfd);
    if (!lss) {
        errno = EINVAL;
        return -1;
    }
    if (fdapi_getsockname(listenfd, (struct sockaddr *)&name, &namelen) == 0)
        family = name.ss_family;

    acceptfd = fdapi_socket(family, SOCK_STREAM, 0);
    if (acceptfd < 0) return -1;

    ass = WSIOCP_GetSocketState(acceptfd);
    if (!ass) {
        fdapi_close(acceptfd);
        errno = EINVAL;
        return -1;
    }

    areq = (aacceptreq *)walloc(sizeof(*areq));
    areq->buf = walloc(ACCEPTEX_ADDR_BUF * 2);
    areq->accept = acceptfd;

    rc = FDAPI_AcceptEx(listenfd, acceptfd, areq->buf, 0,
                        ACCEPTEX_ADDR_BUF, ACCEPTEX_ADDR_BUF,
                        &bytes, &areq->ov);
    if (SUCCEEDED_WITH_IOCP(rc)) {
        lss->masks |= ACCEPT_PENDING;
        return 0;
    }
    errno = FDAPI_WSAGetLastError();
    lss->masks &= ~ACCEPT_PENDING;
    fdapi_close(acceptfd);
    wfree(areq->buf);
    wfree(areq);
    return -1;
}

int WSIOCP_Listen(int rfd, int backlog) {
    iocpSockState *ss = WSIOCP_GetSocketState(rfd);
    if (!ss) {
        errno = EINVAL;
        return -1;
    }
    if (fdapi_listen(rfd, backlog) != 0)
        return -1;
    ss->masks |= LISTEN_SOCK;
    return 0;
}

int WSIOCP_Accept(int fd, struct sockaddr *sa, socklen_t *len) {
    iocpSockState *ss;
    aacceptreq *areq;
    int acceptfd;
    struct sockaddr *local = NULL, *remote = NULL;
    int locallen = 0, remotelen = 0;

    ss = WSIOCP_GetSocketState(fd);
    if (!ss) {
        errno = EINVAL;
        return -1;
    }
    areq = ss->reqs;
    if (!areq) {
        errno = EAGAIN;
        return -1;
    }
    ss->reqs = areq->next;
    acceptfd = areq->accept;

    if (FDAPI_UpdateAcceptContext(acceptfd, fd) != 0) {
        wfree(areq->buf);
        wfree(areq);
        return -1;
    }
    FDAPI_GetAcceptExSockaddrs(acceptfd, areq->buf, 0,
                               ACCEPTEX_ADDR_BUF, ACCEPTEX_ADDR_BUF,
                               &local, &locallen, &remote, &remotelen);
    if (sa && len) {
        if (remote && remotelen > 0) {
            if (remotelen < *len) *len = (socklen_t)remotelen;
            memcpy(sa, remote, *len);
        } else {
            *len = 0;
        }
    }
    wfree(areq->buf);
    wfree(areq);
    WSIOCP_QueueAccept(fd);
    return acceptfd;
}

int WSIOCP_SocketSend(int fd, char *buf, int len, void *eventLoop,
                      void *client, void *data, void *proc) {
    iocpSockState *ss = WSIOCP_GetSocketState(fd);
    asendreq *areq;
    unsigned long sent = 0;
    int rc;

    if (ss && (ss->masks & CONNECT_PENDING))
        aeWait(fd, AE_WRITABLE, 50);

    if (!ss || (ss->masks & SOCKET_ATTACHED) == 0 || !proc)
        return (int)fdapi_write(fd, buf, (size_t)len);

    areq = (asendreq *)walloc(sizeof(*areq));
    areq->wbuf.len = (unsigned long)len;
    areq->wbuf.buf = buf;
    areq->eventLoop = (aeEventLoop *)eventLoop;
    areq->req.client = client;
    areq->req.data = data;
    areq->req.len = len;
    areq->req.buf = buf;
    areq->proc = (aeFileProc *)proc;

    rc = FDAPI_WSASend(fd, &areq->wbuf, 1, &sent, 0, &areq->ov, NULL);
    if (SUCCEEDED_WITH_IOCP(rc == 0)) {
        errno = EAGAIN;
        ss->wreqs++;
        areq->next = ss->wreqlist;
        ss->wreqlist = areq;
        return -1;
    }
    errno = FDAPI_WSAGetLastError();
    wfree(areq);
    return -1;
}

int WSIOCP_SocketConnect(int rfd, const struct sockaddr *addr, socklen_t len) {
    iocpSockState *ss = WSIOCP_GetSocketState(rfd);
    int rc;

    if (!ss) {
        errno = EINVAL;
        return -1;
    }
    if (ss->iocp == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (WSIOCP_Associate(rfd, ss->iocp) < 0)
        return -1;

    memset(&ss->ov_read, 0, sizeof(ss->ov_read));
    if (addr->sa_family == AF_INET) {
        struct sockaddr_in any;
        memset(&any, 0, sizeof(any));
        any.sin_family = AF_INET;
        fdapi_bind(rfd, (struct sockaddr *)&any, sizeof(any));
    } else if (addr->sa_family == AF_INET6) {
        struct sockaddr_in6 any;
        memset(&any, 0, sizeof(any));
        any.sin6_family = AF_INET6;
        fdapi_bind(rfd, (struct sockaddr *)&any, sizeof(any));
    }

    rc = FDAPI_ConnectEx(rfd, addr, (int)len, NULL, 0, NULL, &ss->ov_read);
    if (rc) return 0;
    rc = FDAPI_WSAGetLastError();
    if (rc == ERROR_IO_PENDING) {
        errno = EINPROGRESS;
        ss->masks |= CONNECT_PENDING;
        return -1;
    }
    errno = rc;
    return -1;
}

int WSIOCP_AddEvent(aeEventLoop *el, int fd, int mask) {
    aeApiState *state = (aeApiState *)el->apidata;
    iocpSockState *ss;
    int assoc;

    ss = WSIOCP_GetSocketState(fd);
    if (!ss) {
        errno = EINVAL;
        return -1;
    }

    assoc = WSIOCP_Associate(fd, state->iocp);
    if (assoc < 0) return -1;
    /* assoc == 1: already on another IOCP; 9.2 forwarding. For 1.2, still arm. */

    if ((ss->masks & LISTEN_SOCK) == 0) {
        int acceptconn = 0;
        socklen_t olen = sizeof(acceptconn);
        if (fdapi_getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN,
                             &acceptconn, &olen) == 0 && acceptconn)
            ss->masks |= LISTEN_SOCK;
    }

    if (mask & AE_READABLE) {
        ss->masks |= AE_READABLE;
        if ((ss->masks & CONNECT_PENDING) == 0) {
            if (ss->masks & LISTEN_SOCK) {
                if ((ss->masks & ACCEPT_PENDING) == 0)
                    WSIOCP_QueueAccept(fd);
            } else if ((ss->masks & READ_QUEUED) == 0) {
                WSIOCP_QueueNextRead(fd);
            }
        }
    }
    if (mask & AE_WRITABLE) {
        ss->masks |= AE_WRITABLE;
        if ((ss->masks & CONNECT_PENDING) == 0 && ss->wreqs == 0) {
            asendreq *areq = (asendreq *)walloc(sizeof(*areq));
            if (!PostQueuedCompletionStatus((HANDLE)state->iocp, 0,
                                            (ULONG_PTR)(intptr_t)fd,
                                            &areq->ov)) {
                errno = GetLastError();
                wfree(areq);
                return -1;
            }
            ss->wreqs++;
            areq->next = ss->wreqlist;
            ss->wreqlist = areq;
        }
    }
    return 0;
}

void WSIOCP_DelEvent(aeEventLoop *el, int fd, int mask) {
    iocpSockState *ss = WSIOCP_GetExistingSocketState(fd);
    (void)el;
    if (!ss) return;
    if (mask & AE_READABLE) ss->masks &= ~AE_READABLE;
    if (mask & AE_WRITABLE) ss->masks &= ~AE_WRITABLE;
}

int WSIOCP_Poll(aeEventLoop *el, struct timeval *tvp) {
    aeApiState *state = (aeApiState *)el->apidata;
    OVERLAPPED_ENTRY entries[MAX_COMPLETE_PER_POLL];
    ULONG numComplete = 0;
    ULONG j;
    int numevents = 0;
    DWORD mswait = tvp ? (DWORD)(tvp->tv_sec * 1000 + tvp->tv_usec / 1000) : 100;
    BOOL rc;

    rc = GetQueuedCompletionStatusEx((HANDLE)state->iocp, entries,
                                     MAX_COMPLETE_PER_POLL, &numComplete,
                                     mswait, FALSE);
    if (!rc || numComplete == 0)
        return 0;

    for (j = 0; j < numComplete && numevents < state->setsize; j++) {
        int rfd = (int)(intptr_t)entries[j].lpCompletionKey;
        iocpSockState *ss = WSIOCP_GetExistingSocketState(rfd);
        LPOVERLAPPED ov = entries[j].lpOverlapped;

        if (!ss) continue;

        if ((ss->masks & CLOSE_PENDING) == 0) {
            if ((ss->masks & LISTEN_SOCK) && ov != NULL) {
                aacceptreq *areq = (aacceptreq *)ov;
                areq->next = ss->reqs;
                ss->reqs = areq;
                ss->masks &= ~ACCEPT_PENDING;
                if (ss->masks & AE_READABLE) {
                    el->fired[numevents].fd = rfd;
                    el->fired[numevents].mask = AE_READABLE;
                    numevents++;
                }
            } else if (ss->masks & CONNECT_PENDING) {
                if (ov == &ss->ov_read) {
                    ss->masks &= ~CONNECT_PENDING;
                    WSIOCP_AddEvent(el, rfd, ss->masks);
                }
            } else {
                int matched = 0;
                if (ov == &ss->ov_read) {
                    matched = 1;
                    ss->masks &= ~READ_QUEUED;
                    if (ss->masks & AE_READABLE) {
                        el->fired[numevents].fd = rfd;
                        el->fired[numevents].mask = AE_READABLE;
                        numevents++;
                    }
                } else if (ss->wreqs > 0 && ov != NULL) {
                    asendreq *areq = (asendreq *)ov;
                    if (unlink_wreq(ss, areq)) {
                        matched = 1;
                        if (areq->proc) {
                            unsigned long written = 0, flags = 0;
                            FDAPI_WSAGetOverlappedResult(rfd, &areq->ov,
                                                         &written, 0, &flags);
                            areq->proc(areq->eventLoop, rfd, &areq->req,
                                       (int)written);
                        }
                        ss->wreqs--;
                        wfree(areq);
                        if (ss->wreqs == 0 && (ss->masks & AE_WRITABLE)) {
                            el->fired[numevents].fd = rfd;
                            el->fired[numevents].mask = AE_WRITABLE;
                            numevents++;
                        }
                    }
                }
                if (!matched && ss->unknownComplete == 0) {
                    ss->unknownComplete = 1;
                    fdapi_close(rfd);
                }
            }
        } else {
            if (ss->masks & CONNECT_PENDING) {
                if (ov == &ss->ov_read)
                    ss->masks &= ~CONNECT_PENDING;
            } else if (ov == &ss->ov_read) {
                ss->masks &= ~READ_QUEUED;
            } else if (ov) {
                asendreq *areq = (asendreq *)ov;
                if (unlink_wreq(ss, areq)) {
                    ss->wreqs--;
                    wfree(areq);
                }
            }
            if (ss->wreqs == 0 &&
                (ss->masks & (CONNECT_PENDING | READ_QUEUED | SOCKET_ATTACHED)) == 0) {
                ss->masks &= ~CLOSE_PENDING;
                if (WSIOCP_CloseSocketState(ss))
                    FDAPI_ClearSocketInfo(rfd);
            }
        }
    }
    return numevents;
}
