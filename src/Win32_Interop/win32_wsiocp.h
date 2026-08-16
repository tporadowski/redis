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

#ifndef WIN32_INTEROP_WSIOCP_H
#define WIN32_INTEROP_WSIOCP_H

#include "../ae.h"
#include "Win32_FDAPI.h"

#ifdef __cplusplus
extern "C" {
#endif

#define READ_QUEUED     0x000100
#define SOCKET_ATTACHED 0x000400
#define ACCEPT_PENDING  0x000800
#define LISTEN_SOCK     0x001000
#define CONNECT_PENDING 0x002000
#define CLOSE_PENDING   0x004000
#define UNIX_LISTEN     0x008000 /* AF_UNIX listen: accept(), not AcceptEx */

typedef struct WSIOCP_Request {
    void *client;
    void *data;
    char *buf;
    int len;
} WSIOCP_Request;

typedef struct aeApiState {
    void *iocp;
    int setsize;
    struct aeEventLoop *el;
    void *fwd_cs; /* CRITICAL_SECTION* */
    int fwd_rfd;
    int fwd_wfd;
    int *fwd_fds;
    int *fwd_masks;
    int fwd_n;
    int fwd_cap;
} aeApiState;

void *WSIOCP_CreateIocp(void);
void WSIOCP_CloseIocp(void *iocp);
void WSIOCP_OnLoopCreated(void);
void *WSIOCP_GetLoopIocp(aeEventLoop *el);
void *WSIOCP_GetAssociatedIocp(int fd);

/* 0 = associated (or already on this IOCP), 1 = already on another IOCP, -1 = error. */
int WSIOCP_Associate(int fd, void *iocp);
/* Delay-associate to el's IOCP, or mark dest_el for forwarding if already attached. */
int WSIOCP_SetDestLoop(int fd, struct aeEventLoop *el);
int WSIOCP_InitLoopExtras(struct aeEventLoop *el);
void WSIOCP_FreeLoopExtras(struct aeEventLoop *el);
/* Post a no-op completion so a thread blocked in GQCS enters beforeSleep. */
void WSIOCP_WakeLoop(struct aeEventLoop *el);

int WSIOCP_AddEvent(aeEventLoop *el, int fd, int mask);
void WSIOCP_DelEvent(aeEventLoop *el, int fd, int mask);
int WSIOCP_Poll(aeEventLoop *el, struct timeval *tvp);

int WSIOCP_QueueNextRead(int rfd);
/* Cancel outstanding zero-byte WSARecv and drain its IOCP completion. */
int WSIOCP_CancelAndDrainRead(int rfd);
/* Re-arm zero-byte WSARecv if AE_READABLE and nothing is queued. */
int WSIOCP_RearmRead(int rfd);
int WSIOCP_QueueAccept(int listenfd);
int WSIOCP_Listen(int rfd, int backlog);
int WSIOCP_Accept(int rfd, struct sockaddr *sa, socklen_t *len);
int WSIOCP_SocketSend(int rfd, char *buf, int len, void *eventLoop,
                      void *client, void *data, void *proc);
int WSIOCP_SocketConnect(int rfd, const struct sockaddr *addr, socklen_t len);
/* Non-zero WSA/errno from a completed ConnectEx; cleared on read. */
int WSIOCP_TakeConnectError(int rfd);
int WSIOCP_CloseSocketStateRFD(int rfd);

#ifdef __cplusplus
}
#endif

#endif
