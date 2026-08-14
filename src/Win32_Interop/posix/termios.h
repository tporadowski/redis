/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_TERMIOS_H
#define WIN32_POSIX_TERMIOS_H

#include "../win32_pre.h"
#include <string.h>

#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define ECHO 0x0008
#define ICANON 0x0002
#define ISIG 0x0001
#define IEXTEN 0x0100
#define BRKINT 0x0002
#define ICRNL 0x0100
#define INPCK 0x0010
#define ISTRIP 0x0020
#define IXON 0x0400
#define OPOST 0x0001
#define CS8 0x0030
#define VMIN 6
#define VTIME 5

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_cc[32];
};

static inline int tcgetattr(int fd, struct termios *t) {
    (void)fd;
    if (t) memset(t, 0, sizeof(*t));
    return -1;
}
static inline int tcsetattr(int fd, int actions, const struct termios *t) {
    (void)fd; (void)actions; (void)t;
    return -1;
}

#endif
