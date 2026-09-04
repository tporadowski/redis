/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "Win32_QFork.h"

#define BLOCK QFORK_BLOCK_SIZE

static int va_committed(void *p) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi)))
        return 0;
    return mbi.State == MEM_COMMIT;
}

int main(void) {
    void *before = AllocHeapBlock(NULL, BLOCK, 1);
    if (before == NULL) {
        fprintf(stderr, "NULL-safe AllocHeapBlock before init failed\n");
        return 1;
    }
    memset(before, 0x11, 64);
    if (!FreeHeapBlock(before, BLOCK)) {
        fprintf(stderr, "FreeHeapBlock fallback failed\n");
        return 1;
    }

    if (!QForkParentInit(BLOCK * 16)) {
        fprintf(stderr, "QForkParentInit failed\n");
        return 2;
    }
    if (!g_HasMemoryMappedHeap || g_pQForkControl == NULL) {
        fprintf(stderr, "heap globals not set after init\n");
        QForkShutdown();
        return 2;
    }

    void *a = AllocHeapBlock(NULL, BLOCK, 1);
    if (a == NULL) {
        fprintf(stderr, "AllocHeapBlock 4MB failed\n");
        QForkShutdown();
        return 3;
    }
    memset(a, 0xAB, 64);

    if (!CommitHeapBlock(a, BLOCK, 0)) {
        fprintf(stderr, "decommit failed\n");
        QForkShutdown();
        return 4;
    }
    if (!CommitHeapBlock(a, BLOCK, 1)) {
        fprintf(stderr, "recommit failed\n");
        QForkShutdown();
        return 4;
    }
    if (!PurgePages(a, BLOCK)) {
        fprintf(stderr, "PurgePages failed\n");
        QForkShutdown();
        return 4;
    }
    if (!FreeHeapBlock(a, BLOCK)) {
        fprintf(stderr, "FreeHeapBlock map failed\n");
        QForkShutdown();
        return 5;
    }
    if (va_committed(a)) {
        fprintf(stderr, "expected unmap after last slot free at %p\n", a);
        QForkShutdown();
        return 5;
    }

    void *b = AllocHeapBlock(NULL, BLOCK, 1);
    if (b != a) {
        fprintf(stderr, "expected recycle of first block %p got %p\n", a, b);
        QForkShutdown();
        return 6;
    }
    FreeHeapBlock(b, BLOCK);

    QForkHoldUnmap(1);
    void *held = AllocHeapBlock(NULL, BLOCK, 0);
    if (held == NULL) {
        fprintf(stderr, "hold alloc failed\n");
        QForkShutdown();
        return 6;
    }
    if (!FreeHeapBlock(held, BLOCK) || !va_committed(held)) {
        fprintf(stderr, "hold should keep the 4MB view mapped\n");
        QForkShutdown();
        return 6;
    }
    QForkHoldUnmap(0);
    if (va_committed(held)) {
        fprintf(stderr, "hold release should unmap empty blocks\n");
        QForkShutdown();
        return 6;
    }
    {
        int i;
        void *p0 = NULL;
        for (i = 0; i < 32; i++) {
            void *p = AllocHeapBlock(NULL, BLOCK, 0);
            if (p == NULL) {
                fprintf(stderr, "remap cycle alloc failed at %d\n", i);
                QForkShutdown();
                return 6;
            }
            if (i == 0)
                p0 = p;
            else if (p != p0) {
                fprintf(stderr, "remap cycle %d expected %p got %p\n", i, p0, p);
                QForkShutdown();
                return 6;
            }
            if (!FreeHeapBlock(p, BLOCK) || va_committed(p)) {
                fprintf(stderr, "remap cycle unmap failed at %d\n", i);
                QForkShutdown();
                return 6;
            }
        }
    }

    /* Partial free: discard the hole, block stays mapped (sibling slot). */
    {
        const size_t slot_sz = QFORK_SLOT_SIZE;
        void *keep = AllocHeapBlock(NULL, slot_sz, 0);
        void *hole = AllocHeapBlock(NULL, slot_sz, 0);
        if (!keep || !hole) {
            fprintf(stderr, "MEM_RESET pair alloc failed\n");
            QForkShutdown();
            return 7;
        }
        memset(hole, 0xAB, slot_sz);
        if (!FreeHeapBlock(hole, slot_sz)) {
            fprintf(stderr, "MEM_RESET hole free failed\n");
            QForkShutdown();
            return 7;
        }
        void *again = AllocHeapBlock(NULL, slot_sz, 0);
        if (again != hole) {
            fprintf(stderr, "MEM_RESET expected recycle %p got %p\n", hole, again);
            QForkShutdown();
            return 7;
        }
        if (((unsigned char *)again)[0] != 0 ||
            ((unsigned char *)again)[slot_sz - 1] != 0) {
            fprintf(stderr, "MEM_RESET hole was not demand-zero\n");
            QForkShutdown();
            return 7;
        }
        FreeHeapBlock(again, slot_sz);
        FreeHeapBlock(keep, slot_sz);
    }

    void *slot = AllocHeapBlock(NULL, QFORK_SLOT_SIZE, 1);
    if (slot == NULL) {
        fprintf(stderr, "slot alloc failed\n");
        QForkShutdown();
        return 7;
    }
    memset(slot, 0xCD, 64);
    if (!FreeHeapBlock(slot, QFORK_SLOT_SIZE)) {
        fprintf(stderr, "slot FreeHeapBlock failed\n");
        QForkShutdown();
        return 7;
    }

    /* Block-first search: many slots, punch holes, refill. */
    {
        enum { N = 32 };
        const size_t slot_sz = QFORK_SLOT_SIZE;
        void *p[N];
        int i;
        for (i = 0; i < N; i++) {
            p[i] = AllocHeapBlock(NULL, slot_sz, 0);
            if (p[i] == NULL) {
                fprintf(stderr, "slot burst failed at %d\n", i);
                QForkShutdown();
                return 9;
            }
        }
        for (i = 0; i < N; i += 2) {
            if (!FreeHeapBlock(p[i], slot_sz)) {
                fprintf(stderr, "slot hole free failed at %d\n", i);
                QForkShutdown();
                return 9;
            }
            p[i] = NULL;
        }
        for (i = 0; i < N; i += 2) {
            p[i] = AllocHeapBlock(NULL, slot_sz, 0);
            if (p[i] == NULL) {
                fprintf(stderr, "slot refill failed at %d\n", i);
                QForkShutdown();
                return 9;
            }
        }
        for (i = 0; i < N; i++)
            FreeHeapBlock(p[i], slot_sz);
    }

    /* Two-block (8 MB) request + recycle. */
    {
        void *big = AllocHeapBlock(NULL, BLOCK * 2, 1);
        if (big == NULL) {
            fprintf(stderr, "8MB alloc failed\n");
            QForkShutdown();
            return 10;
        }
        memset(big, 0xEF, 64);
        if (!FreeHeapBlock(big, BLOCK * 2)) {
            fprintf(stderr, "8MB free failed\n");
            QForkShutdown();
            return 10;
        }
        void *big2 = AllocHeapBlock(NULL, BLOCK * 2, 0);
        if (big2 != big) {
            fprintf(stderr, "8MB expected recycle %p got %p\n", big, big2);
            QForkShutdown();
            return 10;
        }
        FreeHeapBlock(big2, BLOCK * 2);
    }

    /* Cross-block: fill the 16-block heap, free last slot of blk0 + first of
     * blk1, then a 2-slot request must take that seam. */
    {
        enum { NS = 16 * (int)QFORK_SLOTS_PER_BLOCK };
        const size_t slot_sz = QFORK_SLOT_SIZE;
        const int seam0 = (int)QFORK_SLOTS_PER_BLOCK - 1;
        const int seam1 = (int)QFORK_SLOTS_PER_BLOCK;
        void *all[NS];
        int i;
        for (i = 0; i < NS; i++) {
            all[i] = AllocHeapBlock(NULL, slot_sz, 0);
            if (all[i] == NULL) {
                fprintf(stderr, "cross-block fill failed at %d\n", i);
                QForkShutdown();
                return 11;
            }
        }
        FreeHeapBlock(all[seam0], slot_sz);
        FreeHeapBlock(all[seam1], slot_sz);
        void *span = AllocHeapBlock(NULL, slot_sz * 2, 0);
        if (span != all[seam0]) {
            fprintf(stderr, "cross-block expected seam %p got %p\n", all[seam0], span);
            QForkShutdown();
            return 11;
        }
        FreeHeapBlock(span, slot_sz * 2);
        for (i = 0; i < NS; i++) {
            if (i == seam0 || i == seam1)
                continue;
            FreeHeapBlock(all[i], slot_sz);
        }
    }

    void *small = AllocHeapBlock(NULL, 4096, 1);
    if (small == NULL) {
        fprintf(stderr, "sub-slot fallback VirtualAlloc failed\n");
        QForkShutdown();
        return 7;
    }
    if (!FreeHeapBlock(small, 4096)) {
        fprintf(stderr, "sub-slot FreeHeapBlock failed\n");
        QForkShutdown();
        return 7;
    }

    void *want = (char *)a;
    void *fixed = AllocHeapBlock(want, BLOCK, 1);
    if (fixed != want) {
        fprintf(stderr, "fixed-addr map wanted %p got %p\n", want, fixed);
        QForkShutdown();
        return 7;
    }
    FreeHeapBlock(fixed, BLOCK);

    /* Parent WRITECOPY dirtied pages must survive rejoin. */
    {
        void *cow = AllocHeapBlock(NULL, BLOCK, 1);
        if (cow == NULL) {
            fprintf(stderr, "cow alloc failed\n");
            QForkShutdown();
            return 12;
        }
        memset(cow, 0x11, 64);
        if (!QForkProtectForFork()) {
            fprintf(stderr, "QForkProtectForFork failed gle=%lu\n", GetLastError());
            QForkShutdown();
            return 12;
        }
        memset(cow, 0x22, 64);
        ((unsigned char *)cow)[BLOCK - 1] = 0x33;
        if (!QForkRejoinAfterFork()) {
            fprintf(stderr, "QForkRejoinAfterFork failed\n");
            QForkShutdown();
            return 12;
        }
        if (((unsigned char *)cow)[0] != 0x22 ||
            ((unsigned char *)cow)[63] != 0x22 ||
            ((unsigned char *)cow)[BLOCK - 1] != 0x33) {
            fprintf(stderr, "rejoin dropped parent COW writes\n");
            QForkShutdown();
            return 12;
        }
        {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(cow, &mbi, sizeof(mbi)) ||
                mbi.Protect == PAGE_WRITECOPY) {
                fprintf(stderr, "rejoin left PAGE_WRITECOPY protect=%lx\n",
                        (unsigned long)mbi.Protect);
                QForkShutdown();
                return 12;
            }
        }
        FreeHeapBlock(cow, BLOCK);
    }

    QForkShutdown();
    if (g_pQForkControl != NULL) {
        fprintf(stderr, "g_pQForkControl not cleared\n");
        return 8;
    }

    printf("ok qfork heap (NULL-safe, map, commit-in-place, recycle, cow-rejoin)\n");
    return 0;
}