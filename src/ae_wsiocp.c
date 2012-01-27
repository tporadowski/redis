/* Copyright (c) 2012, Microsoft Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice, 
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
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

/* IOCP-based ae.c module  */

#include <string.h>
#include "ae.h"
#include "win32fixes.h"
#include "zmalloc.h"
#include "win32_wsiocp.h"
#include <mswsock.h>
#include <Guiddef.h>


#define MAX_COMPLETE_PER_POLL       100

/* structure that keeps state of sockets and Completion port handle */
typedef struct aeApiState {
    HANDLE iocp;
    int setsize;
    OVERLAPPED_ENTRY entries[MAX_COMPLETE_PER_POLL];
    aeSockState *sockstate;
} aeApiState;


/* utility to validate that socket / fd is being monitored */
aeSockState *aeGetSockState(void *apistate, int fd) {
    if (apistate == NULL) return NULL;
    if (fd >= ((aeApiState *)apistate)->setsize) {
        return NULL;
    }
    return &((aeApiState *)apistate)->sockstate[fd];
}

/* Called by ae to initialize state */
static int aeApiCreate(aeEventLoop *eventLoop) {
    aeApiState *state = (aeApiState *)zmalloc(sizeof(aeApiState));

    if (!state) return -1;
    memset(state, 0, sizeof(aeApiState));

    state->sockstate = (aeSockState *)zmalloc(sizeof(aeSockState) * AE_SETSIZE);
    if (state->sockstate == NULL) {
        zfree(state);
        return -1;
    }

    /* create a single IOCP to be shared by all sockets */
    state->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
                                         NULL,
                                         0,
                                         1);

    state->setsize = AE_SETSIZE;
    eventLoop->apidata = state;
    /* initialize the IOCP socket code with state reference */
    aeWinInit(state, state->iocp, aeGetSockState);
    return 0;
}

/* termination */
static void aeApiFree(aeEventLoop *eventLoop) {
    aeApiState *state = (aeApiState *)eventLoop->apidata;
    CloseHandle(state->iocp);
    zfree(state->sockstate);
    zfree(state);
    aeWinCleanup();
}

/* monitor state changes for a socket */
static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = (aeApiState *)eventLoop->apidata;
    aeSockState *sockstate = aeGetSockState(state, fd);
    if (sockstate == NULL) {
        errno = WSAEINVAL;
        return -1;
    }

    if (mask & AE_READABLE) {
        sockstate->masks |= AE_READABLE;
        if (sockstate->masks & LISTEN_SOCK) {
            /* actually a listen. Do not treat as read */
        } else {
            if ((sockstate->masks & READ_QUEUED) == 0) {
                // queue up a 0 byte read
                aeWinReceiveDone(fd);
            }
        }
    }
    if (mask & AE_WRITABLE) {
        sockstate->masks |= AE_WRITABLE;
        // if no write active, then need to queue write ready
        if (sockstate->wreqs == 0) {
            asendreq *areq = (asendreq *)zmalloc(sizeof(asendreq));
            memset(areq, 0, sizeof(asendreq));
            if (PostQueuedCompletionStatus(state->iocp,
                                        0,
                                        fd,
                                        &areq->ov) == 0) {
                errno = GetLastError();
                zfree(areq);
                return -1;
            }
            sockstate->wreqs++;
        }
    }
    return 0;
}

/* stop monitoring state changes for a socket */
static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState *state = (aeApiState *)eventLoop->apidata;
    aeSockState *sockstate = aeGetSockState(state, fd);
    if (sockstate == NULL) {
        errno = WSAEINVAL;
        return;
    }

    if (mask & AE_READABLE) sockstate->masks &= ~AE_READABLE;
    if (mask & AE_WRITABLE) sockstate->masks &= ~AE_WRITABLE;
}

/* return array of sockets that are ready for read or write 
   depending on the mask for each socket */
static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    aeApiState *state = (aeApiState *)eventLoop->apidata;
    aeSockState *sockstate;
    ULONG j;
    int numevents = 0;
    ULONG numComplete = 0;
    int rc;
    int mswait = (tvp->tv_sec * 1000) + (tvp->tv_usec / 1000);

    /* first get an array of completion notifications */
    rc = GetQueuedCompletionStatusEx(state->iocp,
                                    state->entries,
                                    MAX_COMPLETE_PER_POLL,
                                    &numComplete,
                                    mswait,
                                    FALSE);
    if (rc && numComplete > 0) {
        LPOVERLAPPED_ENTRY entry = state->entries;
        for (j = 0; j < numComplete && numevents < AE_SETSIZE; j++, entry++) {
            /* the competion key is the socket */
            SOCKET sock = (SOCKET)entry->lpCompletionKey;
            sockstate = aeGetSockState(state, sock);
            if (sockstate == NULL)  continue;

            if (sockstate->masks & LISTEN_SOCK) {
                /* need to set event for listening */
                aacceptreq *areq = (aacceptreq *)entry->lpOverlapped;
                areq->next = sockstate->reqs;
                sockstate->reqs = areq;
                sockstate->masks &= ~ACCEPT_PENDING;
                if (sockstate->masks & AE_READABLE) {
                    eventLoop->fired[numevents].fd = sock;
                    eventLoop->fired[numevents].mask = AE_READABLE;
                    numevents++;
                }
            } else {
                /* check if event is read complete (may be 0 length read) */
                if (entry->lpOverlapped == &sockstate->ov_read &&
                    entry->lpOverlapped->Internal != STATUS_PENDING) {
                    sockstate->masks &= ~READ_QUEUED;
                   if (sockstate->masks & AE_READABLE) {
                        eventLoop->fired[numevents].fd = sock;
                        eventLoop->fired[numevents].mask = AE_READABLE;
                        numevents++;
                    }
                } else if (sockstate->wreqs > 0) {
                    /* should be write complete. Get results */
                    asendreq *areq = (asendreq *)entry->lpOverlapped;
                    /* call write complete callback so buffers can be freed */
                    if (areq->proc != NULL) {
                        DWORD written = 0;
                        DWORD flags;
                        WSAGetOverlappedResult(sock, &areq->ov, &written, FALSE, &flags);
                        areq->proc(areq->eventLoop, sock, &areq->req, (int)written);
                    }
                    sockstate->wreqs--;
                    zfree(areq);
                    /* if no active write requests, set ready to write */
                    if (sockstate->wreqs == 0 && sockstate->masks & AE_WRITABLE) {
                        eventLoop->fired[numevents].fd = sock;
                        eventLoop->fired[numevents].mask = AE_WRITABLE;
                        numevents++;
                    } else {
                    }
                }
            }
        }
    }
    return numevents;
}

/* name of this event handler */
static char *aeApiName(void) {
    return "winsock_IOCP";
}



