/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_SYS_IOCTL_H
#define WIN32_POSIX_SYS_IOCTL_H
#define FIONREAD 0x4004667F
#define TIOCGWINSZ 0x5413
struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};
static inline int ioctl(int fd, unsigned long req, ...) {
    (void)fd; (void)req;
    return -1;
}
#endif
