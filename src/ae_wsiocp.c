/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/* Per-loop IOCP ae backend. One IOCP per aeEventLoop; no process-global iocph.
 * Sockets are associated on the first aeApiAddEvent (delay-associate). */

#include "Win32_Interop/win32_wsiocp.h"

static int aeApiCreate(aeEventLoop *eventLoop) {
    aeApiState *state = zcalloc(sizeof(*state));
    if (!state) return -1;
    state->iocp = WSIOCP_CreateIocp();
    if (!state->iocp) {
        zfree(state);
        return -1;
    }
    state->setsize = eventLoop->setsize;
    eventLoop->apidata = state;
    WSIOCP_OnLoopCreated();
    if (WSIOCP_InitLoopExtras(eventLoop) != 0) {
        WSIOCP_CloseIocp(state->iocp);
        zfree(state);
        eventLoop->apidata = NULL;
        return -1;
    }
    return 0;
}

static int aeApiResize(aeEventLoop *eventLoop, int setsize) {
    ((aeApiState *)eventLoop->apidata)->setsize = setsize;
    return 0;
}

static void aeApiFree(aeEventLoop *eventLoop) {
    aeApiState *state = eventLoop->apidata;
    WSIOCP_FreeLoopExtras(eventLoop);
    WSIOCP_CloseIocp(state->iocp);
    zfree(state);
}

static int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    return WSIOCP_AddEvent(eventLoop, fd, mask);
}

static void aeApiDelEvent(aeEventLoop *eventLoop, int fd, int mask) {
    WSIOCP_DelEvent(eventLoop, fd, mask);
}

static int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    return WSIOCP_Poll(eventLoop, tvp);
}

static char *aeApiName(void) {
    return "WinSock_IOCP";
}
