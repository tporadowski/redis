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
#include <stdlib.h>
#include <string.h>

#include "Win32_QFork.h"
#include "Win32_EventLog.h"

/*
 * Mapping unit stays 4 MB (COW / CreateFileMapping). jemalloc 5.3 uses
 * LG_PAGE=16 (64 KB): a 4 MB PAGE made edata bitmaps ~66 KB and OOMed a
 * 16-byte zmalloc after ~10k successful ones (aeCreate). Each 4 MB block
 * is 64 slots of 64 KB.
 */
#define QFORK_LG_BLOCK 22
#define QFORK_BLOCK_SIZE ((size_t)1 << QFORK_LG_BLOCK)
#define QFORK_LG_SLOT 16
#define QFORK_SLOT_SIZE ((size_t)1 << QFORK_LG_SLOT)
#define QFORK_SLOTS_PER_BLOCK (QFORK_BLOCK_SIZE / QFORK_SLOT_SIZE)
#define QFORK_MAX_BLOCKS (1 << (40 - QFORK_LG_BLOCK)) /* 4 MB * 256K = 1 TB */

enum BlockState : uint8_t {
    bsINVALID = 0,
    bsUNMAPPED = 1,
    bsMAPPED_IN_USE = 2,
    bsMAPPED_FREE = 3
};

struct heapBlockInfo {
    HANDLE heapMap;
    uint64_t used; /* bit i set → 64 KB slot i in use */
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
static int heap_log_on(void);

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
        DWORD gle = GetLastError();
        char ev[256];
        snprintf(ev, sizeof(ev),
                 "CreateFileMapping/pagefile: CreateBlockMap gle=%lu", gle);
        fprintf(stderr, "%s\n", ev);
        WriteEventLogError(ev);
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
        DWORD gle = GetLastError();
        char ev[256];
        snprintf(ev, sizeof(ev),
                 "QForkParentInit failed: CreateFileMapping gle=%lu", gle);
        fprintf(stderr, "%s\n", ev);
        WriteEventLogError(ev);
        return 0;
    }

    g_pQForkControl = MapViewOfFile(g_hQForkControlFileMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!g_pQForkControl) {
        DWORD gle = GetLastError();
        char ev[256];
        snprintf(ev, sizeof(ev),
                 "QForkParentInit failed: MapViewOfFile gle=%lu", gle);
        fprintf(stderr, "%s\n", ev);
        WriteEventLogError(ev);
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
    uint64_t max_heap = (uint64_t)heap_bytes;
    if (max_heap == 0) {
        /* Tests set QFORK_HEAP_BYTES (decimal, or suffix M/G) so we do not
         * reserve a 10×RAM VA on every tcl-spawned server. */
        const char *env = getenv("QFORK_HEAP_BYTES");
        if (env && env[0]) {
            char *end = NULL;
            unsigned long long n = strtoull(env, &end, 10);
            if (end) {
                if (*end == 'M' || *end == 'm')
                    n *= 1024ull * 1024ull;
                else if (*end == 'G' || *end == 'g')
                    n *= 1024ull * 1024ull * 1024ull;
            }
            max_heap = (uint64_t)n;
        }
    }
    if (max_heap == 0)
        max_heap = memstatus.ullTotalPhys * 10;
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
        DWORD gle = GetLastError();
        char ev[256];
        snprintf(ev, sizeof(ev),
                 "CreateFileMapping/pagefile: VirtualAlloc reserve gle=%lu",
                 gle);
        fprintf(stderr, "%s\n", ev);
        WriteEventLogError(ev);
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
        c->heapBlockList[i].used = 0;
    }
    for (int i = c->maxAvailableBlocks; i < QFORK_MAX_BLOCKS; i++) {
        c->heapBlockList[i].state = bsINVALID;
        c->heapBlockList[i].heapMap = NULL;
        c->heapBlockList[i].used = 0;
    }

    g_HasMemoryMappedHeap = 1;
    g_BypassMemoryMapOnAlloc = 0;
    if (heap_log_on())
        fprintf(stderr, "QForkParentInit: heap [%p, %p) blocks=%d (%.1f GB)\n",
                c->heapStart, c->heapEnd, c->maxAvailableBlocks,
                (double)c->maxAvailableBlocks * QFORK_BLOCK_SIZE /
                    (1024.0 * 1024.0 * 1024.0));
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

static int heap_log_on(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *e = getenv("QFORK_HEAP_LOG");
        cached = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return cached;
}

static unsigned g_alloc_log_n;
static void heap_log(const char *what, void *addr, size_t size, void *got) {
    if (!heap_log_on() && got != NULL)
        return;
    if (g_alloc_log_n < 40 || got == NULL)
        fprintf(stderr, "AllocHeapBlock[%u] %s addr=%p size=%zu -> %p gle=%lu\n",
                g_alloc_log_n, what, addr, size, got, GetLastError());
    g_alloc_log_n++;
}

static int slot_is_free(QForkControl *c, int slot) {
    int blk = slot / (int)QFORK_SLOTS_PER_BLOCK;
    int bit = slot % (int)QFORK_SLOTS_PER_BLOCK;
    if (blk < 0 || blk >= c->maxAvailableBlocks)
        return 0;
    if (c->heapBlockList[blk].state == bsINVALID)
        return 0;
    return (c->heapBlockList[blk].used & (1ull << bit)) == 0;
}

static int slots_range_free(QForkControl *c, int start, int need) {
    for (int i = 0; i < need; i++) {
        if (!slot_is_free(c, start + i))
            return 0;
    }
    return 1;
}

static int map_block_if_needed(QForkControl *c, int blk) {
    if (c->heapBlockList[blk].heapMap)
        return 1;
    HANDLE map = CreateBlockMap(blk);
    if (!map)
        return 0;
    c->heapBlockList[blk].heapMap = map;
    c->heapBlockList[blk].used = 0;
    c->numMappedBlocks += 1;
    return 1;
}

static void mark_slots_used(QForkControl *c, int start, int need, int zero) {
    for (int i = 0; i < need; i++) {
        int slot = start + i;
        int blk = slot / (int)QFORK_SLOTS_PER_BLOCK;
        int bit = slot % (int)QFORK_SLOTS_PER_BLOCK;
        c->heapBlockList[blk].used |= (1ull << bit);
        c->heapBlockList[blk].state = bsMAPPED_IN_USE;
        if (zero) {
            LPVOID p = (BYTE *)c->heapStart + (size_t)slot * QFORK_SLOT_SIZE;
            SecureZeroMemory(p, QFORK_SLOT_SIZE);
        }
    }
}

void *AllocHeapBlock(void *addr, size_t size, int zero) {
    if (g_pQForkControl == NULL || g_BypassMemoryMapOnAlloc) {
        void *p = VirtualAlloc(addr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        heap_log(g_pQForkControl ? "bypass" : "null-ctrl", addr, size, p);
        return p;
    }

    if (size == 0) {
        errno = EINVAL;
        heap_log("size0", addr, size, NULL);
        return NULL;
    }
    /* jemalloc PAGE is 64 KB. Slow-path oversize (size+align-os_page) is not
     * a 64 KB multiple — those stay on VirtualAlloc. */
    if ((size % QFORK_SLOT_SIZE) != 0) {
        void *place = (addr && !addr_in_heap(addr)) ? addr : NULL;
        void *p = VirtualAlloc(place, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!p) {
            fprintf(stderr,
                    "AllocHeapBlock: VirtualAlloc(%p,%zu) fallback gle=%lu\n",
                    place, size, GetLastError());
            errno = ENOMEM;
        }
        heap_log(place ? "va-fixed" : "va-fallback", addr, size, p);
        return p;
    }

    QForkControl *c = ctrl();
    int need = (int)(size / QFORK_SLOT_SIZE);
    int total_slots = c->maxAvailableBlocks * (int)QFORK_SLOTS_PER_BLOCK;
    int allocAt;

    if (addr != NULL) {
        if (!addr_in_heap(addr) ||
            (((uintptr_t)addr - (uintptr_t)c->heapStart) % QFORK_SLOT_SIZE) != 0) {
            fprintf(stderr, "AllocHeapBlock: honor-addr %p not a heap slot\n",
                    addr);
            errno = EINVAL;
            heap_log("honor-bad", addr, size, NULL);
            return NULL;
        }
        allocAt = (int)(((BYTE *)addr - (BYTE *)c->heapStart) / QFORK_SLOT_SIZE);
        if (allocAt < 0 || allocAt + need > total_slots) {
            errno = ENOMEM;
            heap_log("honor-oob", addr, size, NULL);
            return NULL;
        }
        if (!slots_range_free(c, allocAt, need)) {
            errno = ENOMEM;
            heap_log("honor-busy", addr, size, NULL);
            return NULL;
        }
    } else {
        int endSearch = total_slots - need;
        allocAt = -1;
        int startIdx = c->blockSearchStart * (int)QFORK_SLOTS_PER_BLOCK;
        if (startIdx < 0) startIdx = 0;
        for (; startIdx <= endSearch; startIdx++) {
            if (slots_range_free(c, startIdx, need)) {
                allocAt = startIdx;
                break;
            }
        }
        if (allocAt < 0) {
            errno = ENOMEM;
            heap_log("search-fail", addr, size, NULL);
            return NULL;
        }
    }

    int first_blk = allocAt / (int)QFORK_SLOTS_PER_BLOCK;
    int last_blk = (allocAt + need - 1) / (int)QFORK_SLOTS_PER_BLOCK;
    for (int blk = first_blk; blk <= last_blk; blk++) {
        if (!map_block_if_needed(c, blk)) {
            errno = ENOMEM;
            heap_log("map-fail", addr, size, NULL);
            return NULL;
        }
    }
    mark_slots_used(c, allocAt, need, zero);

    if (addr == NULL) {
        int next_blk = (allocAt + need) / (int)QFORK_SLOTS_PER_BLOCK;
        if (next_blk > c->blockSearchStart)
            c->blockSearchStart = next_blk;
    }

    void *got = (BYTE *)c->heapStart + (size_t)allocAt * QFORK_SLOT_SIZE;
    heap_log(addr ? "mapped-fixed" : "mapped", addr, size, got);
    return got;
}

int FreeHeapBlock(void *addr, size_t size) {
    if (size == 0)
        return FALSE;

    if (!g_HasMemoryMappedHeap || !addr_in_heap(addr)) {
        return VirtualFree(addr, 0, MEM_RELEASE) ? TRUE : FALSE;
    }

    QForkControl *c = ctrl();
    size_t ptrDiff = (BYTE *)addr - (BYTE *)c->heapStart;
    if ((ptrDiff % QFORK_SLOT_SIZE) != 0)
        return FALSE;

    if ((size % QFORK_SLOT_SIZE) != 0)
        size = (size + QFORK_SLOT_SIZE - 1) & ~(QFORK_SLOT_SIZE - 1);

    int slotStart = (int)(ptrDiff / QFORK_SLOT_SIZE);
    int n = (int)(size / QFORK_SLOT_SIZE);
    int total_slots = c->maxAvailableBlocks * (int)QFORK_SLOTS_PER_BLOCK;
    if (slotStart < 0 || slotStart + n > total_slots)
        return FALSE;

    for (int i = 0; i < n; i++) {
        int slot = slotStart + i;
        int blk = slot / (int)QFORK_SLOTS_PER_BLOCK;
        int bit = slot % (int)QFORK_SLOTS_PER_BLOCK;
        c->heapBlockList[blk].used &= ~(1ull << bit);
        if (c->heapBlockList[blk].used == 0 &&
            c->heapBlockList[blk].state == bsMAPPED_IN_USE)
            c->heapBlockList[blk].state = bsMAPPED_FREE;
    }

    int first_blk = slotStart / (int)QFORK_SLOTS_PER_BLOCK;
    if (c->blockSearchStart > first_blk)
        c->blockSearchStart = first_blk;
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
