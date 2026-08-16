/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/*
 * jemalloc page-hook fallbacks (3.1). VirtualAlloc until 3.2 installs the
 * QFork mapped heap. AllocHeapBlock is NULL-safe.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "Win32_QFork.h"

void *g_pQForkControl = NULL;
int g_BypassMemoryMapOnAlloc = 0;

void *AllocHeapBlock(void *addr, size_t size, int zero) {
    (void)zero;
    if (g_pQForkControl == NULL || g_BypassMemoryMapOnAlloc) {
        return VirtualAlloc(addr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }
    /* 3.2: walk g_pQForkControl block list. Unreachable until then. */
    return VirtualAlloc(addr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

int FreeHeapBlock(void *addr, size_t size) {
    (void)size;
    return VirtualFree(addr, 0, MEM_RELEASE) ? TRUE : FALSE;
}

int PurgePages(void *addr, size_t length) {
    return VirtualAlloc(addr, length, MEM_RESET, PAGE_READWRITE) != NULL
        ? TRUE : FALSE;
}

int CommitHeapBlock(void *addr, size_t size, int commit) {
    if (commit) {
        return VirtualAlloc(addr, size, MEM_COMMIT, PAGE_READWRITE) == addr
            ? TRUE : FALSE;
    }
    return VirtualFree(addr, size, MEM_DECOMMIT) ? TRUE : FALSE;
}
