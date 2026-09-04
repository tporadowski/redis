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
 * Mapping unit stays 4 MB (COW / CreateFileMapping). jemalloc LG_PAGE is
 * QFORK_LG_SLOT (1 MB): 4 MB pages OOMed a 16-byte zmalloc (aeCreate).
 * Alloc search is block-first (skip a full used[] in O(1)), then a bit-run
 * inside the block.
 */
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
static volatile long g_unmap_hold;
static SRWLOCK g_heapLock = SRWLOCK_INIT;
static int heap_log_on(void);
static void unmap_block_if_empty(QForkControl *c, int blk);
static int purge_pages_unlocked(void *addr, size_t length);

class QForkHeapLockGuard {
public:
    QForkHeapLockGuard() { AcquireSRWLockExclusive(&g_heapLock); }
    ~QForkHeapLockGuard() { ReleaseSRWLockExclusive(&g_heapLock); }
private:
    QForkHeapLockGuard(const QForkHeapLockGuard&);
    QForkHeapLockGuard& operator=(const QForkHeapLockGuard&);
};

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
    /* Drop the MEM_RESERVE hole we left after a previous unmap. */
    {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) &&
            mbi.AllocationBase == addr && mbi.State == MEM_RESERVE)
            VirtualFree(addr, 0, MEM_RELEASE);
    }
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
    QForkHeapLockGuard lock;
    QForkControl *c = ctrl();
    if (c) {
        for (int i = 0; i < c->maxAvailableBlocks; i++) {
            LPVOID addr = (BYTE *)c->heapStart + (size_t)i * QFORK_BLOCK_SIZE;
            if (c->heapBlockList[i].state == bsMAPPED_IN_USE ||
                c->heapBlockList[i].state == bsMAPPED_FREE) {
                UnmapViewOfFile(addr);
                if (c->heapBlockList[i].heapMap) {
                    CloseHandle(c->heapBlockList[i].heapMap);
                    c->heapBlockList[i].heapMap = NULL;
                }
            } else if (c->heapBlockList[i].state == bsUNMAPPED) {
                MEMORY_BASIC_INFORMATION mbi;
                if (VirtualQuery(addr, &mbi, sizeof(mbi)) &&
                    mbi.AllocationBase == addr && mbi.State == MEM_RESERVE)
                    VirtualFree(addr, 0, MEM_RELEASE);
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

#define QFORK_SLOT_ALL_USED \
    ((QFORK_SLOTS_PER_BLOCK >= 64) ? ~0ull : ((1ull << QFORK_SLOTS_PER_BLOCK) - 1))

static int block_valid(QForkControl *c, int blk) {
    return blk >= 0 && blk < c->maxAvailableBlocks &&
           c->heapBlockList[blk].state != bsINVALID;
}

static int block_fully_used(QForkControl *c, int blk) {
    if (!block_valid(c, blk))
        return 1;
    if (c->heapBlockList[blk].state == bsUNMAPPED)
        return 0;
    return c->heapBlockList[blk].used == QFORK_SLOT_ALL_USED;
}

static int block_fully_free(QForkControl *c, int blk) {
    if (!block_valid(c, blk))
        return 0;
    if (c->heapBlockList[blk].state == bsUNMAPPED)
        return 1;
    return c->heapBlockList[blk].used == 0;
}

static int block_free_suffix(QForkControl *c, int blk) {
    int spb = (int)QFORK_SLOTS_PER_BLOCK;
    if (!block_valid(c, blk))
        return 0;
    if (c->heapBlockList[blk].state == bsUNMAPPED)
        return spb;
    uint64_t used = c->heapBlockList[blk].used;
    int n = 0;
    for (int b = spb - 1; b >= 0 && (used & (1ull << b)) == 0; b--)
        n++;
    return n;
}

static int block_free_prefix(QForkControl *c, int blk, int want) {
    int spb = (int)QFORK_SLOTS_PER_BLOCK;
    if (want <= 0)
        return 1;
    if (!block_valid(c, blk))
        return 0;
    if (c->heapBlockList[blk].state == bsUNMAPPED)
        return want <= spb;
    if (want > spb)
        return 0;
    uint64_t mask = (want == spb) ? QFORK_SLOT_ALL_USED : ((1ull << want) - 1);
    return (c->heapBlockList[blk].used & mask) == 0;
}

/* First index of `need` consecutive 0-bits in `used`, or -1. */
static int find_zero_run(uint64_t used, int need) {
    int spb = (int)QFORK_SLOTS_PER_BLOCK;
    if (need <= 0 || need > spb)
        return -1;
    if (need == spb)
        return used == 0 ? 0 : -1;
    uint64_t mask = (1ull << need) - 1;
    int last = spb - need;
    for (int b = 0; b <= last; b++) {
        if ((used & (mask << b)) == 0)
            return b;
    }
    return -1;
}

/* Block-first range check (not a per-slot walk). */
static int slots_range_free(QForkControl *c, int start, int need) {
    int spb = (int)QFORK_SLOTS_PER_BLOCK;
    int total = c->maxAvailableBlocks * spb;
    if (start < 0 || need <= 0 || start + need > total)
        return 0;
    int slot = start;
    int left = need;
    while (left > 0) {
        int blk = slot / spb;
        int bit = slot % spb;
        if (!block_valid(c, blk))
            return 0;
        int take = spb - bit;
        if (take > left)
            take = left;
        if (c->heapBlockList[blk].state != bsUNMAPPED) {
            uint64_t mask = (take == spb) ? QFORK_SLOT_ALL_USED : ((1ull << take) - 1);
            if (c->heapBlockList[blk].used & (mask << bit))
                return 0;
        }
        slot += take;
        left -= take;
    }
    return 1;
}

static int find_in_block(QForkControl *c, int blk, int need) {
    int spb = (int)QFORK_SLOTS_PER_BLOCK;
    if (need <= 0 || need > spb || !block_valid(c, blk))
        return -1;
    if (c->heapBlockList[blk].state == bsUNMAPPED)
        return blk * spb;
    if (c->heapBlockList[blk].used == QFORK_SLOT_ALL_USED)
        return -1;
    int bit = find_zero_run(c->heapBlockList[blk].used, need);
    if (bit < 0)
        return -1;
    return blk * spb + bit;
}

/* `need` slots starting at a block boundary: full blocks + prefix of the last. */
static int find_multiblock(QForkControl *c, int lo, int hi, int need) {
    int spb = (int)QFORK_SLOTS_PER_BLOCK;
    int full = need / spb;
    int rem = need % spb;
    int nblk = full + (rem ? 1 : 0);
    int last_start = c->maxAvailableBlocks - nblk;
    if (last_start < 0)
        return -1;
    if (hi > last_start)
        hi = last_start;
    for (int blk = lo; blk <= hi; blk++) {
        int ok = 1;
        for (int i = 0; i < full; i++) {
            if (!block_fully_free(c, blk + i)) {
                ok = 0;
                break;
            }
        }
        if (!ok)
            continue;
        if (rem && !block_free_prefix(c, blk + full, rem))
            continue;
        return blk * spb;
    }
    return -1;
}

/* Suffix of blk + prefix of blk+1 (need fits in two blocks). */
static int find_cross_block(QForkControl *c, int lo, int hi, int need) {
    int spb = (int)QFORK_SLOTS_PER_BLOCK;
    if (need < 2 || need > 2 * spb - 1)
        return -1;
    if (hi > c->maxAvailableBlocks - 2)
        hi = c->maxAvailableBlocks - 2;
    for (int blk = lo; blk <= hi; blk++) {
        if (block_fully_used(c, blk) || block_fully_used(c, blk + 1))
            continue;
        int suffix = block_free_suffix(c, blk);
        if (suffix == 0 || suffix >= need)
            continue;
        if (block_free_prefix(c, blk + 1, need - suffix))
            return blk * spb + (spb - suffix);
    }
    return -1;
}

/* Large request that starts mid-block (free suffix + following blocks). */
static int find_multiblock_mid(QForkControl *c, int lo, int hi, int need) {
    int spb = (int)QFORK_SLOTS_PER_BLOCK;
    if (hi > c->maxAvailableBlocks - 1)
        hi = c->maxAvailableBlocks - 1;
    for (int blk = lo; blk <= hi; blk++) {
        if (block_fully_used(c, blk))
            continue;
        int suffix = block_free_suffix(c, blk);
        if (suffix == 0)
            continue;
        int start = blk * spb + (spb - suffix);
        if (slots_range_free(c, start, need))
            return start;
    }
    return -1;
}

/*
 * 5.0 searched 4 MB blocks. We still allocate 64 KB slots, but the walk
 * is per-block (skip full used[] in O(1)), then a bit-run inside the block.
 */
static int find_free_slots(QForkControl *c, int need) {
    int nblk = c->maxAvailableBlocks;
    int start = c->blockSearchStart;
    if (start < 0 || start >= nblk)
        start = 0;
    int spb = (int)QFORK_SLOTS_PER_BLOCK;

    if (need <= spb) {
        for (int blk = start; blk < nblk; blk++) {
            int at = find_in_block(c, blk, need);
            if (at >= 0)
                return at;
        }
        for (int blk = 0; blk < start; blk++) {
            int at = find_in_block(c, blk, need);
            if (at >= 0)
                return at;
        }
        int at = find_cross_block(c, start, nblk - 1, need);
        if (at >= 0)
            return at;
        return find_cross_block(c, 0, start - 1, need);
    }

    int at = find_multiblock(c, start, nblk - 1, need);
    if (at >= 0)
        return at;
    at = find_multiblock(c, 0, start - 1, need);
    if (at >= 0)
        return at;
    at = find_multiblock_mid(c, start, nblk - 1, need);
    if (at >= 0)
        return at;
    return find_multiblock_mid(c, 0, start - 1, need);
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

    QForkHeapLockGuard lock;
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
        allocAt = find_free_slots(c, need);
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

static void unmap_block_if_empty(QForkControl *c, int blk) {
    if (!block_valid(c, blk) || c->heapBlockList[blk].used != 0)
        return;
    if (c->heapBlockList[blk].state == bsUNMAPPED)
        return;
    /* Child process, or parent during a live --QFork COW: keep the section
     * so QForkChildAttach can DuplicateHandle. */
    if (g_BypassMemoryMapOnAlloc || g_unmap_hold > 0) {
        if (c->heapBlockList[blk].state == bsMAPPED_IN_USE)
            c->heapBlockList[blk].state = bsMAPPED_FREE;
        return;
    }
    HANDLE map = c->heapBlockList[blk].heapMap;
    LPVOID base = (BYTE *)c->heapStart + (size_t)blk * QFORK_BLOCK_SIZE;
    if (map) {
        UnmapViewOfFile(base);
        CloseHandle(map);
        c->heapBlockList[blk].heapMap = NULL;
        if (c->numMappedBlocks > 0)
            c->numMappedBlocks -= 1;
    }
    /* Hold the VA so a later MapViewOfFileEx can reuse this block. */
    VirtualAlloc(base, QFORK_BLOCK_SIZE, MEM_RESERVE, PAGE_NOACCESS);
    c->heapBlockList[blk].state = bsUNMAPPED;
}

void QForkHoldUnmap(int hold) {
    if (hold) {
        InterlockedIncrement(&g_unmap_hold);
        return;
    }
    LONG v = InterlockedDecrement(&g_unmap_hold);
    if (v < 0) {
        InterlockedIncrement(&g_unmap_hold);
        return;
    }
    if (v != 0)
        return;
    QForkHeapLockGuard lock;
    QForkControl *c = ctrl();
    if (!c || g_BypassMemoryMapOnAlloc)
        return;
    for (int i = 0; i < c->maxAvailableBlocks; i++)
        unmap_block_if_empty(c, i);
}

int FreeHeapBlock(void *addr, size_t size) {
    if (size == 0)
        return FALSE;

    QForkHeapLockGuard lock;

    if (!g_HasMemoryMappedHeap || !addr_in_heap(addr)) {
        return VirtualFree(addr, 0, MEM_RELEASE) ? TRUE : FALSE;
    }

    /* Child: new allocs are VirtualAlloc (bypass). jemalloc may still
     * dallocx an inherited mapped pointer. Do not clear used[] or unmap
     * the snapshot the child is serializing. */
    if (g_BypassMemoryMapOnAlloc) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(addr, &mbi, sizeof(mbi)))
            return FALSE;
        if (mbi.Type != MEM_MAPPED)
            return VirtualFree(addr, 0, MEM_RELEASE) ? TRUE : FALSE;
        return TRUE;
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

    int first_blk = slotStart / (int)QFORK_SLOTS_PER_BLOCK;
    int last_blk = (slotStart + n - 1) / (int)QFORK_SLOTS_PER_BLOCK;
    for (int i = 0; i < n; i++) {
        int slot = slotStart + i;
        int blk = slot / (int)QFORK_SLOTS_PER_BLOCK;
        int bit = slot % (int)QFORK_SLOTS_PER_BLOCK;
        c->heapBlockList[blk].used &= ~(1ull << bit);
    }
    /* Tell the kernel these pages are trash so they drop from the working
     * set. Skip in the QFork child (COW dataset). Ignore failure — the
     * slot is still free. Empty blocks unmap next and do not need RESET. */
    if (!g_BypassMemoryMapOnAlloc)
        purge_pages_unlocked(addr, size);
    for (int blk = first_blk; blk <= last_blk; blk++)
        unmap_block_if_empty(c, blk);

    if (c->blockSearchStart > first_blk)
        c->blockSearchStart = first_blk;
    return TRUE;
}

static int purge_pages_unlocked(void *addr, size_t length) {
    if (!addr || length == 0)
        return FALSE;
    /* DiscardVirtualMemory (Win8.1+) actually drops mapped views.
     * MEM_RESET on a pagefile MapViewOfFile does not demand-zero (17.4 smoke). */
    typedef DWORD (WINAPI *discard_fn)(PVOID, SIZE_T);
    static discard_fn discard;
    static int resolved;
    if (!resolved) {
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        if (k32)
            discard = (discard_fn)(void *)GetProcAddress(k32, "DiscardVirtualMemory");
        resolved = 1;
    }
    if (discard && discard(addr, length) == ERROR_SUCCESS)
        return TRUE;
    return VirtualAlloc(addr, length, MEM_RESET, PAGE_READWRITE) != NULL ? TRUE : FALSE;
}

int PurgePages(void *addr, size_t length) {
    QForkHeapLockGuard lock;
    return purge_pages_unlocked(addr, length);
}

void *QForkGetControlMap(void) {
    return (void *)g_hQForkControlFileMap;
}

int QForkProtectForFork(void) {
    QForkHeapLockGuard lock;
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

/* Copy dirtied WRITECOPY pages into a writable view of the section, then
 * discard the private view and remap FILE_MAP_ALL_ACCESS at the same VA.
 * A block protected while in use can be freed before the child exits; it
 * still has private COW pages that must be merged. */
static int RejoinCOWPages(HANDLE mmHandle, BYTE *mmStart, size_t mmSize) {
    DWORD error = ERROR_SUCCESS;
    const char *operation = NULL;

    if (!mmHandle || !mmStart || mmSize == 0)
        return 0;

    BYTE *copyView = (BYTE *)MapViewOfFile(mmHandle, FILE_MAP_WRITE, 0, 0, mmSize);
    if (!copyView) {
        fprintf(stderr, "RejoinCOWPages: FILE_MAP_WRITE view gle=%lu\n",
                GetLastError());
        return 0;
    }

    for (BYTE *mmAddress = mmStart; mmAddress < mmStart + mmSize; ) {
        MEMORY_BASIC_INFORMATION memInfo;
        if (!VirtualQuery(mmAddress, &memInfo, sizeof(memInfo))) {
            error = GetLastError();
            operation = "VirtualQuery";
            break;
        }
        BYTE *regionEnd = (BYTE *)memInfo.BaseAddress + memInfo.RegionSize;
        if (regionEnd <= mmAddress) {
            error = ERROR_INVALID_ADDRESS;
            operation = "VirtualQuery region";
            break;
        }
        if (memInfo.Protect != PAGE_WRITECOPY) {
            BYTE *srcEnd = regionEnd < mmStart + mmSize ? regionEnd
                                                        : mmStart + mmSize;
            memcpy(copyView + (mmAddress - mmStart), mmAddress,
                   (size_t)(srcEnd - mmAddress));
        }
        mmAddress = regionEnd;
    }

    if (error == ERROR_SUCCESS && !UnmapViewOfFile(mmStart)) {
        error = GetLastError();
        operation = "UnmapViewOfFile";
    }

    if (error == ERROR_SUCCESS) {
        BYTE *remapped = (BYTE *)MapViewOfFileEx(mmHandle, FILE_MAP_ALL_ACCESS,
                                                 0, 0, 0, mmStart);
        if (remapped != mmStart) {
            error = remapped ? ERROR_INVALID_ADDRESS : GetLastError();
            operation = "MapViewOfFileEx";
            if (remapped)
                UnmapViewOfFile(remapped);
        }
    }

    if (!UnmapViewOfFile(copyView) && error == ERROR_SUCCESS) {
        error = GetLastError();
        operation = "unmap copy view";
    }

    if (error != ERROR_SUCCESS) {
        fprintf(stderr, "RejoinCOWPages: %s gle=%lu at %p size=%zu\n",
                operation ? operation : "unknown", error, mmStart, mmSize);
        return 0;
    }
    return 1;
}

int QForkRejoinAfterFork(void) {
    QForkHeapLockGuard lock;
    QForkControl *c = ctrl();
    if (!c)
        return 1;
    /* Merge every still-mapped block, including ones freed (MAPPED_FREE)
     * while the child was alive. */
    for (int i = 0; i < c->maxAvailableBlocks; i++) {
        if (!c->heapBlockList[i].heapMap)
            continue;
        if (c->heapBlockList[i].state != bsMAPPED_IN_USE &&
            c->heapBlockList[i].state != bsMAPPED_FREE)
            continue;
        LPVOID addr = (BYTE *)c->heapStart + (size_t)i * QFORK_BLOCK_SIZE;
        if (!RejoinCOWPages(c->heapBlockList[i].heapMap, (BYTE *)addr,
                            QFORK_BLOCK_SIZE)) {
            fprintf(stderr, "QForkRejoinAfterFork: block %d failed\n", i);
            return 0;
        }
    }
    if (g_hQForkControlFileMap && c) {
        if (!RejoinCOWPages(g_hQForkControlFileMap, (BYTE *)c,
                            sizeof(QForkControl))) {
            fprintf(stderr, "QForkRejoinAfterFork: control map failed\n");
            return 0;
        }
        /* Rejoin remaps at the same VA; g_pQForkControl is still valid. */
    }
    return 1;
}

int CommitHeapBlock(void *addr, size_t size, int commit) {
    QForkHeapLockGuard lock;
    if (g_HasMemoryMappedHeap && addr_in_heap(addr)) {
        /* Cannot MEM_DECOMMIT a pagefile view. jemalloc still calls decommit
         * on decay/arena.purge — DiscardVirtualMemory so the working set
         * actually drops. Recommit is demand-zero on next touch. */
        if (!commit && !g_BypassMemoryMapOnAlloc)
            purge_pages_unlocked(addr, size);
        return TRUE;
    }
    if (commit)
        return VirtualAlloc(addr, size, MEM_COMMIT, PAGE_READWRITE) == addr ? TRUE : FALSE;
    return VirtualFree(addr, size, MEM_DECOMMIT) ? TRUE : FALSE;
}
