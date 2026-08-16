/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_INTEROP_QFORK_H
#define WIN32_INTEROP_QFORK_H

#ifdef _WIN32

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int redis_main(int argc, char **argv);

/*
 * jemalloc page hooks. AllocHeapBlock is NULL-safe: if g_pQForkControl is
 * NULL (pages_boot before QForkStartup, check tools, sentinel) it never
 * dereferences the map — VirtualAlloc fallback.
 *
 * No MAX_REDIS_DATA_SIZE. Fork payload is a separate mapped section (3.3).
 */
extern void *g_pQForkControl;
extern int g_BypassMemoryMapOnAlloc;
extern int g_HasMemoryMappedHeap;

/* heap_bytes == 0 → default (10× physical RAM, cap 1 TB). */
int QForkParentInit(size_t heap_bytes);
void QForkShutdown(void);

void *AllocHeapBlock(void *addr, size_t size, int zero);
int FreeHeapBlock(void *addr, size_t size);
int PurgePages(void *addr, size_t length);
int CommitHeapBlock(void *addr, size_t size, int commit);

#ifndef QFORK_MAIN_IMPL
#define main redis_main
#endif

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */
#endif
