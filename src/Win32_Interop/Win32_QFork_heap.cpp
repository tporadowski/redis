/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/*
 * QFork pagefile heap (3.2). Parent reserves 4 MB blocks; AllocHeapBlock
 * maps them on demand. No MAX_REDIS_DATA_SIZE — payload is 3.3.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "Win32_QFork.h"

/* Must match jemalloc LG_PAGE=22. */
#define QFORK_LG_PAGE 22
#define QFORK_BLOCK_SIZE ((size_t)1 << QFORK_LG_PAGE)
#define QFORK_MAX_BLOCKS (1 << (40 - QFORK_LG_PAGE)) /* 4 MB * 256K = 1 TB */

enum BlockState : uint8_t {
    bsINVALID = 0,
    bsUNMAPPED = 1,
    bsMAPPED_IN_USE = 2,
    bsMAPPED_FREE = 3
};

struct heapBlockInfo {
    HANDLE heapMap;
    BlockState state;
};

struct QForkControl {
    LPVOID heapStart;
    LPVOID heapEnd;
    int maxAvailableBlocks;
    int numMappedBlocks;
    int blockSearchStart;
    heapBlockInfo heapBlockList[QFORK_MAX_BLOCKS];
};

void *g_pQForkControl = NULL;
int g_BypassMemoryMapOnAlloc = 0;
int g_HasMemoryMappedHeap = 0;
int g_PersistenceDisabled = 0;

static HANDLE g_hQForkControlFileMap = NULL;

static QForkControl *ctrl(void) {
    return (QForkControl *)g_pQForkControl;
}

static int addr_in_heap(void *addr) {
    QForkControl *c = ctrl();
    return c && addr >= c->heapStart && addr < c->heapEnd;
}

static HANDLE CreateBlockMap(int blockIndex) {
    HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                    0, (DWORD)QFORK_BLOCK_SIZE, NULL);
    if (!map) {
        fprintf(stderr, "CreateBlockMap: CreateFileMapping failed gle=%lu\n",
                GetLastError());
        return NULL;
    }

    LPVOID addr = (BYTE *)ctrl()->heapStart + (size_t)blockIndex * QFORK_BLOCK_SIZE;
    if (!MapViewOfFileEx(map, FILE_MAP_ALL_ACCESS, 0, 0, 0, addr)) {
        fprintf(stderr, "CreateBlockMap: MapViewOfFileEx(%p) failed gle=%lu\n",
                addr, GetLastError());
        CloseHandle(map);
        return NULL;
    }
    return map;
}

int QForkParentInit(size_t heap_bytes) {
    g_hQForkControlFileMap = CreateFileMappingW(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
        0, (DWORD)sizeof(QForkControl), NULL);
    if (!g_hQForkControlFileMap) {
        fprintf(stderr,
                "QForkParentInit: CreateFileMapping failed gle=%lu\n",
                GetLastError());
        return 0;
    }

    g_pQForkControl = MapViewOfFile(g_hQForkControlFileMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!g_pQForkControl) {
        fprintf(stderr, "QForkParentInit: MapViewOfFile failed gle=%lu\n", GetLastError());
        CloseHandle(g_hQForkControlFileMap);
        g_hQForkControlFileMap = NULL;
        return 0;
    }
    memset(g_pQForkControl, 0, sizeof(QForkControl));

    MEMORYSTATUSEX memstatus;
    memstatus.dwLength = sizeof(MEMORYSTATUSEX);
    if (!GlobalMemoryStatusEx(&memstatus)) {
        fprintf(stderr, "QForkParentInit: GlobalMemoryStatusEx failed gle=%lu\n",
                GetLastError());
        QForkShutdown();
        return 0;
    }

    uint64_t cap = (uint64_t)QFORK_BLOCK_SIZE * QFORK_MAX_BLOCKS;
    uint64_t max_heap = heap_bytes ? (uint64_t)heap_bytes : memstatus.ullTotalPhys * 10;
    if (max_heap > cap)
        max_heap = cap;
    if (max_heap < QFORK_BLOCK_SIZE)
        max_heap = QFORK_BLOCK_SIZE;

    QForkControl *c = ctrl();
    c->maxAvailableBlocks = (int)(max_heap / QFORK_BLOCK_SIZE);
    c->blockSearchStart = 0;
    c->numMappedBlocks = 0;

    SIZE_T reserve_bytes = (SIZE_T)(c->maxAvailableBlocks + 1) * QFORK_BLOCK_SIZE;
    LPVOID pHigh = VirtualAlloc(NULL, reserve_bytes, MEM_RESERVE | MEM_TOP_DOWN,
                                PAGE_READWRITE);
    if (!pHigh) {
        fprintf(stderr, "QForkParentInit: VirtualAlloc reserve failed gle=%lu\n",
                GetLastError());
        QForkShutdown();
        return 0;
    }
    if (!VirtualFree(pHigh, 0, MEM_RELEASE)) {
        fprintf(stderr, "QForkParentInit: VirtualFree probe failed gle=%lu\n",
                GetLastError());
        QForkShutdown();
        return 0;
    }

    uintptr_t start = ((uintptr_t)pHigh + QFORK_BLOCK_SIZE) -
                      ((uintptr_t)pHigh % QFORK_BLOCK_SIZE);
    c->heapStart = (LPVOID)start;
    c->heapEnd = (LPVOID)(start + (uintptr_t)(c->maxAvailableBlocks + 1) * QFORK_BLOCK_SIZE);

    /* Hold the VA with one reservation, then release it. Blocks are mapped
     * on demand — 80k per-block VirtualAlloc VADs OOMs later kernel allocs. */
    for (int i = 0; i < c->maxAvailableBlocks; i++) {
        c->heapBlockList[i].state = bsUNMAPPED;
        c->heapBlockList[i].heapMap = NULL;
    }
    for (int i = c->maxAvailableBlocks; i < QFORK_MAX_BLOCKS; i++) {
        c->heapBlockList[i].state = bsINVALID;
        c->heapBlockList[i].heapMap = NULL;
    }

    g_HasMemoryMappedHeap = 1;
    g_BypassMemoryMapOnAlloc = 0;
    return 1;
}

void QForkShutdown(void) {
    QForkControl *c = ctrl();
    if (c) {
        for (int i = 0; i < c->maxAvailableBlocks; i++) {
            if (c->heapBlockList[i].state == bsMAPPED_IN_USE ||
                c->heapBlockList[i].state == bsMAPPED_FREE) {
                LPVOID addr = (BYTE *)c->heapStart + (size_t)i * QFORK_BLOCK_SIZE;
                UnmapViewOfFile(addr);
                if (c->heapBlockList[i].heapMap) {
                    CloseHandle(c->heapBlockList[i].heapMap);
                    c->heapBlockList[i].heapMap = NULL;
                }
            }
        }
        UnmapViewOfFile(c);
        g_pQForkControl = NULL;
    }
    if (g_hQForkControlFileMap) {
        CloseHandle(g_hQForkControlFileMap);
        g_hQForkControlFileMap = NULL;
    }
    g_HasMemoryMappedHeap = 0;
}

void *AllocHeapBlock(void *addr, size_t size, int zero) {
    (void)zero;
    if (g_pQForkControl == NULL || g_BypassMemoryMapOnAlloc) {
        return VirtualAlloc(addr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    }

    if (size == 0) {
        errno = EINVAL;
        return NULL;
    }
    /* Slow-path maps are only 4 KB aligned (size+align-os_page). Those stay
     * on VirtualAlloc; the QFork heap is 4 MB blocks for jemalloc PAGE. */
    if ((size % QFORK_BLOCK_SIZE) != 0) {
        void *place = (addr && !addr_in_heap(addr)) ? addr : NULL;
        void *p = VirtualAlloc(place, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!p) {
            fprintf(stderr,
                    "AllocHeapBlock: VirtualAlloc(%p,%zu) fallback gle=%lu\n",
                    place, size, GetLastError());
            errno = ENOMEM;
        }
        return p;
    }

    QForkControl *c = ctrl();
    int need = (int)(size / QFORK_BLOCK_SIZE);
    int allocAt;

    /* 5.3 pages_trim remaps at a specific VA. Honor in-heap addr. */
    if (addr != NULL) {
        if (!addr_in_heap(addr) ||
            (((uintptr_t)addr - (uintptr_t)c->heapStart) % QFORK_BLOCK_SIZE) != 0) {
            fprintf(stderr, "AllocHeapBlock: honor-addr %p not a heap block\n",
                    addr);
            errno = EINVAL;
            return NULL;
        }
        allocAt = (int)(((BYTE *)addr - (BYTE *)c->heapStart) / QFORK_BLOCK_SIZE);
        if (allocAt < 0 || allocAt + need > c->maxAvailableBlocks) {
            errno = ENOMEM;
            return NULL;
        }
        for (int i = 0; i < need; i++) {
            BlockState st = c->heapBlockList[allocAt + i].state;
            if (st != bsUNMAPPED && st != bsMAPPED_FREE) {
                errno = ENOMEM;
                return NULL;
            }
        }
    } else {
        int endSearch = c->maxAvailableBlocks - need;
        allocAt = -1;
        for (int startIdx = c->blockSearchStart; startIdx < endSearch; startIdx++) {
            int ok = 1;
            for (int i = 0; i < need; i++) {
                BlockState st = c->heapBlockList[startIdx + i].state;
                if (st != bsUNMAPPED && st != bsMAPPED_FREE) {
                    startIdx += i;
                    ok = 0;
                    break;
                }
            }
            if (ok) {
                allocAt = startIdx;
                break;
            }
        }
        if (allocAt < 0) {
            errno = ENOMEM;
            return NULL;
        }
    }

    for (int i = 0; i < need; i++) {
        int index = allocAt + i;
        if (c->heapBlockList[index].state == bsUNMAPPED) {
            HANDLE map = CreateBlockMap(index);
            if (!map) {
                errno = ENOMEM;
                return NULL;
            }
            c->heapBlockList[index].heapMap = map;
            c->numMappedBlocks += 1;
        } else if (zero) {
            LPVOID ptr = (BYTE *)c->heapStart + (size_t)index * QFORK_BLOCK_SIZE;
            SecureZeroMemory(ptr, QFORK_BLOCK_SIZE);
        }
        c->heapBlockList[index].state = bsMAPPED_IN_USE;
    }

    if (addr == NULL && allocAt == c->blockSearchStart)
        c->blockSearchStart = allocAt + need;

    return (BYTE *)c->heapStart + (size_t)allocAt * QFORK_BLOCK_SIZE;
}

int FreeHeapBlock(void *addr, size_t size) {
    if (size == 0)
        return FALSE;

    if (!g_HasMemoryMappedHeap || !addr_in_heap(addr)) {
        return VirtualFree(addr, 0, MEM_RELEASE) ? TRUE : FALSE;
    }

    QForkControl *c = ctrl();
    size_t ptrDiff = (BYTE *)addr - (BYTE *)c->heapStart;
    if ((ptrDiff % QFORK_BLOCK_SIZE) != 0)
        return FALSE;

    if ((size % QFORK_BLOCK_SIZE) != 0)
        size = (size + QFORK_BLOCK_SIZE - 1) & ~(QFORK_BLOCK_SIZE - 1);

    int blockStart = (int)(ptrDiff / QFORK_BLOCK_SIZE);
    int n = (int)(size / QFORK_BLOCK_SIZE);
    if (blockStart < 0 || blockStart + n > c->maxAvailableBlocks)
        return FALSE;

    for (int i = 0; i < n; i++)
        c->heapBlockList[blockStart + i].state = bsMAPPED_FREE;

    if (c->blockSearchStart > blockStart)
        c->blockSearchStart = blockStart;
    return TRUE;
}

int PurgePages(void *addr, size_t length) {
    return VirtualAlloc(addr, length, MEM_RESET, PAGE_READWRITE) != NULL ? TRUE : FALSE;
}

void *QForkGetControlMap(void) {
    return (void *)g_hQForkControlFileMap;
}

int QForkProtectForFork(void) {
    QForkControl *c = ctrl();
    if (!c)
        return 1;
    DWORD old;
    if (!VirtualProtect(c, sizeof(QForkControl), PAGE_WRITECOPY, &old))
        return 0;
    for (int i = 0; i < c->maxAvailableBlocks; i++) {
        if (c->heapBlockList[i].state != bsMAPPED_IN_USE)
            continue;
        LPVOID addr = (BYTE *)c->heapStart + (size_t)i * QFORK_BLOCK_SIZE;
        if (!VirtualProtect(addr, QFORK_BLOCK_SIZE, PAGE_WRITECOPY, &old))
            return 0;
    }
    return 1;
}

int QForkChildAttach(void *parent_process, void *control_map) {
    HANDLE parent = (HANDLE)parent_process;
    HANDLE local_ctl = NULL;
    if (!DuplicateHandle(parent, (HANDLE)control_map, GetCurrentProcess(),
                         &local_ctl, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        fprintf(stderr, "QForkChildAttach: DuplicateHandle control gle=%lu\n",
                GetLastError());
        return 0;
    }
    QForkControl *view = (QForkControl *)MapViewOfFile(local_ctl, FILE_MAP_COPY,
                                                       0, 0, 0);
    if (!view) {
        fprintf(stderr, "QForkChildAttach: MapViewOfFile control gle=%lu\n",
                GetLastError());
        CloseHandle(local_ctl);
        return 0;
    }
    g_pQForkControl = view;
    g_hQForkControlFileMap = local_ctl;
    g_HasMemoryMappedHeap = 1;
    g_BypassMemoryMapOnAlloc = 1; /* new allocs in the child stay off the COW map */

    for (int i = 0; i < view->maxAvailableBlocks; i++) {
        if (view->heapBlockList[i].state != bsMAPPED_IN_USE)
            continue;
        HANDLE local_heap = NULL;
        if (!DuplicateHandle(parent, view->heapBlockList[i].heapMap,
                             GetCurrentProcess(), &local_heap, 0, FALSE,
                             DUPLICATE_SAME_ACCESS)) {
            fprintf(stderr, "QForkChildAttach: dup heap %d gle=%lu\n", i,
                    GetLastError());
            return 0;
        }
        LPVOID addr = (BYTE *)view->heapStart + (size_t)i * QFORK_BLOCK_SIZE;
        if (!MapViewOfFileEx(local_heap, FILE_MAP_COPY, 0, 0, QFORK_BLOCK_SIZE,
                             addr)) {
            fprintf(stderr, "QForkChildAttach: map heap %d at %p gle=%lu\n",
                    i, addr, GetLastError());
            return 0;
        }
        view->heapBlockList[i].heapMap = local_heap;
    }
    return 1;
}

int QForkRejoinAfterFork(void) {
    QForkControl *c = ctrl();
    if (!c)
        return 1;
    for (int i = 0; i < c->maxAvailableBlocks; i++) {
        if (c->heapBlockList[i].state != bsMAPPED_IN_USE ||
            !c->heapBlockList[i].heapMap)
            continue;
        LPVOID addr = (BYTE *)c->heapStart + (size_t)i * QFORK_BLOCK_SIZE;
        UnmapViewOfFile(addr);
        if (!MapViewOfFileEx(c->heapBlockList[i].heapMap, FILE_MAP_ALL_ACCESS,
                             0, 0, 0, addr)) {
            fprintf(stderr, "QForkRejoinAfterFork: remap %d gle=%lu\n",
                    i, GetLastError());
            return 0;
        }
    }
    if (g_hQForkControlFileMap && c) {
        UnmapViewOfFile(c);
        g_pQForkControl = MapViewOfFile(g_hQForkControlFileMap, FILE_MAP_ALL_ACCESS,
                                        0, 0, 0);
        if (!g_pQForkControl)
            return 0;
    }
    return 1;
}

int CommitHeapBlock(void *addr, size_t size, int commit) {
    if (g_HasMemoryMappedHeap && addr_in_heap(addr)) {
        /*
         * Pagefile views cannot MEM_DECOMMIT/MEM_COMMIT the way VirtualAlloc
         * regions can. Leave them committed; PurgePages (MEM_RESET) is the
         * discard path. Returning success keeps jemalloc from treating a
         * failed VirtualFree as OOM on a live slab.
         */
        (void)size;
        (void)commit;
        return TRUE;
    }
    if (commit)
        return VirtualAlloc(addr, size, MEM_COMMIT, PAGE_READWRITE) == addr ? TRUE : FALSE;
    return VirtualFree(addr, size, MEM_DECOMMIT) ? TRUE : FALSE;
}
