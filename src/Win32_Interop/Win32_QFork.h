/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_INTEROP_QFORK_H
#define WIN32_INTEROP_QFORK_H

#ifdef _WIN32

#ifdef __cplusplus
extern "C" {
#endif

int redis_main(int argc, char **argv);

/*
 * jemalloc page hooks (3.1). Implementations are VirtualAlloc fallbacks until
 * 3.2 installs the QFork mapped heap. AllocHeapBlock is NULL-safe: if
 * g_pQForkControl is NULL it never dereferences the map.
 */
extern void *g_pQForkControl;
extern int g_BypassMemoryMapOnAlloc;

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
