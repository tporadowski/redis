/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/* Per-loop IOCP + delay-associate smoke. */
#include "ae.h"
#include "Win32_Interop/Win32_FDAPI.h"
#include "Win32_Interop/win32_wsiocp.h"
#include "Win32_Interop/posix/unistd.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>

static int got_read;

static void on_read(aeEventLoop *el, int fd, void *data, int mask) {
    (void)el;
    (void)fd;
    (void)data;
    (void)mask;
    got_read = 1;
}

int main(void) {
    aeEventLoop *el1, *el2;
    void *iocp1, *iocp2;
    int ls, cs, as;
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    int one = 1;
    char buf[8];

    FDAPI_Init();

    el1 = aeCreateEventLoop(1024);
    el2 = aeCreateEventLoop(1024);
    if (!el1 || !el2) {
        fprintf(stderr, "aeCreateEventLoop failed\n");
        return 1;
    }
    iocp1 = WSIOCP_GetLoopIocp(el1);
    iocp2 = WSIOCP_GetLoopIocp(el2);
    if (!iocp1 || !iocp2 || iocp1 == iocp2) {
        fprintf(stderr, "expected distinct per-loop IOCPs\n");
        return 1;
    }

    ls = socket(AF_INET, SOCK_STREAM, 0);
    cs = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 3 || cs < 3) {
        fprintf(stderr, "socket failed\n");
        return 1;
    }
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7f000001);
    addr.sin_port = htons(0);
    if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(ls, 1) != 0) {
        fprintf(stderr, "bind/listen failed errno=%d\n", errno);
        return 1;
    }
    alen = sizeof(addr);
    if (getsockname(ls, (struct sockaddr *)&addr, &alen) != 0 ||
        connect(cs, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "connect failed errno=%d\n", errno);
        return 1;
    }
    as = accept(ls, NULL, NULL);
    if (as < 3) {
        fprintf(stderr, "accept failed errno=%d\n", errno);
        return 1;
    }
    if (WSIOCP_GetAssociatedIocp(as) != NULL) {
        fprintf(stderr, "accepted socket associated too early\n");
        return 1;
    }
    if (aeCreateFileEvent(el1, as, AE_READABLE, on_read, NULL) != AE_OK) {
        fprintf(stderr, "aeCreateFileEvent failed errno=%d\n", errno);
        return 1;
    }
    if (WSIOCP_GetAssociatedIocp(as) != iocp1) {
        fprintf(stderr, "delay-associate did not bind to el1\n");
        return 1;
    }

    if (write(cs, "x", 1) != 1) {
        fprintf(stderr, "write failed\n");
        return 1;
    }
    aeProcessEvents(el1, AE_FILE_EVENTS | AE_DONT_WAIT);
    if (!got_read) {
        /* Zero-byte recv may complete on the next poll. */
        aeProcessEvents(el1, AE_FILE_EVENTS | AE_DONT_WAIT);
    }
    if (!got_read) {
        fprintf(stderr, "el1 did not see readable completion\n");
        return 1;
    }
    if (read(as, buf, sizeof(buf)) != 1 || buf[0] != 'x') {
        fprintf(stderr, "payload read mismatch\n");
        return 1;
    }

    got_read = 0;
    aeProcessEvents(el2, AE_FILE_EVENTS | AE_DONT_WAIT);
    if (got_read) {
        fprintf(stderr, "el2 should not see el1 completions\n");
        return 1;
    }

    aeDeleteFileEvent(el1, as, AE_READABLE);
    close(as);
    close(cs);
    close(ls);
    aeDeleteEventLoop(el1);
    aeDeleteEventLoop(el2);
    printf("wsiocp_smoke: ok (per-loop IOCP, delay-associate)\n");
    return 0;
}
