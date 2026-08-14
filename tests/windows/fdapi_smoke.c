/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/* Loopback socket + pipe smoke for FDAPI / RFDMap. */
#include "Win32_Interop/Win32_FDAPI.h"
#include "Win32_Interop/posix/unistd.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>

int main(void) {
    int ls, cs, as;
    struct sockaddr_in addr;
    socklen_t alen = sizeof(addr);
    char buf[16];
    int pfd[2];
    int one = 1;

    FDAPI_Init();

    ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 3) {
        fprintf(stderr, "socket listen failed errno=%d\n", errno);
        return 1;
    }
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7f000001);
    addr.sin_port = htons(0);
    if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "bind failed errno=%d\n", errno);
        return 1;
    }
    if (getsockname(ls, (struct sockaddr *)&addr, &alen) != 0) {
        fprintf(stderr, "getsockname failed errno=%d\n", errno);
        return 1;
    }
    if (listen(ls, 1) != 0) {
        fprintf(stderr, "listen failed errno=%d\n", errno);
        return 1;
    }

    cs = socket(AF_INET, SOCK_STREAM, 0);
    if (cs < 3) {
        fprintf(stderr, "socket client failed\n");
        return 1;
    }
    if (connect(cs, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "connect failed errno=%d\n", errno);
        return 1;
    }
    as = accept(ls, NULL, NULL);
    if (as < 3) {
        fprintf(stderr, "accept failed errno=%d\n", errno);
        return 1;
    }
    if (write(cs, "ping", 4) != 4) {
        fprintf(stderr, "write failed errno=%d\n", errno);
        return 1;
    }
    if (read(as, buf, sizeof(buf)) != 4 || memcmp(buf, "ping", 4) != 0) {
        fprintf(stderr, "read mismatch\n");
        return 1;
    }
    close(as);
    close(cs);
    close(ls);

    if (pipe(pfd) != 0) {
        fprintf(stderr, "pipe failed errno=%d\n", errno);
        return 1;
    }
    if (write(pfd[1], "ok", 2) != 2 || read(pfd[0], buf, 2) != 2) {
        fprintf(stderr, "pipe io failed\n");
        return 1;
    }
    close(pfd[0]);
    close(pfd[1]);

    printf("fdapi_smoke: ok (RFDs, TCP loopback, pipe)\n");
    return 0;
}
