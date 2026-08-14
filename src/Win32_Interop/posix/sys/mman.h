/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_SYS_MMAN_H
#define WIN32_POSIX_SYS_MMAN_H
#include <stddef.h>
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_NONE  0
#define MAP_PRIVATE 2
#define MAP_ANON    0x20
#define MAP_ANONYMOUS MAP_ANON
#define MAP_FAILED ((void *)-1)
#define MADV_DONTNEED 4
#define MADV_FREE 8
static inline void *mmap(void *addr, size_t len, int prot, int flags, int fd, long off) {
    (void)addr; (void)len; (void)prot; (void)flags; (void)fd; (void)off;
    return MAP_FAILED;
}
static inline int munmap(void *addr, size_t len) { (void)addr; (void)len; return -1; }
static inline int mprotect(void *addr, size_t len, int prot) {
    (void)addr; (void)len; (void)prot;
    return -1;
}
static inline int madvise(void *addr, size_t len, int advice) {
    (void)addr; (void)len; (void)advice;
    return -1;
}
#endif
