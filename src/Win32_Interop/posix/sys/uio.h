/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_SYS_UIO_H
#define WIN32_POSIX_SYS_UIO_H
#include <stddef.h>
#ifndef IOV_MAX
#define IOV_MAX 16
#endif
struct iovec {
    void *iov_base;
    size_t iov_len;
};
#ifdef __cplusplus
extern "C" {
#endif
int writev(int fd, const struct iovec *iov, int iovcnt);
#ifdef __cplusplus
}
#endif
#endif
