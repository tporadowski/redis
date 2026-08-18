/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "Win32_QFork.h"

#define BLOCK ((size_t)1 << 22)

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

    void *slot = AllocHeapBlock(NULL, (size_t)1 << 16, 1);
    if (slot == NULL) {
        fprintf(stderr, "64KB slot alloc failed\n");
        QForkShutdown();
        return 7;
    }
    memset(slot, 0xCD, 64);
    if (!FreeHeapBlock(slot, (size_t)1 << 16)) {
        fprintf(stderr, "64KB FreeHeapBlock failed\n");
        QForkShutdown();
        return 7;
    }

    /* Block-first search: many 64 KB slots, punch holes, refill. */
    {
        enum { N = 128 };
        const size_t slot_sz = (size_t)1 << 16;
        void *p[N];
        int i;
        for (i = 0; i < N; i++) {
            p[i] = AllocHeapBlock(NULL, slot_sz, 0);
            if (p[i] == NULL) {
                fprintf(stderr, "64KB burst failed at %d\n", i);
                QForkShutdown();
                return 9;
            }
        }
        for (i = 0; i < N; i += 2) {
            if (!FreeHeapBlock(p[i], slot_sz)) {
                fprintf(stderr, "64KB hole free failed at %d\n", i);
                QForkShutdown();
                return 9;
            }
            p[i] = NULL;
        }
        for (i = 0; i < N; i += 2) {
            p[i] = AllocHeapBlock(NULL, slot_sz, 0);
            if (p[i] == NULL) {
                fprintf(stderr, "64KB refill failed at %d\n", i);
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
     * blk1, then a 128 KB request must take that seam (not a later hole). */
    {
        enum { NS = 16 * 64 };
        const size_t slot_sz = (size_t)1 << 16;
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
        FreeHeapBlock(all[63], slot_sz);
        FreeHeapBlock(all[64], slot_sz);
        void *span = AllocHeapBlock(NULL, slot_sz * 2, 0);
        if (span != all[63]) {
            fprintf(stderr, "cross-block expected seam %p got %p\n", all[63], span);
            QForkShutdown();
            return 11;
        }
        FreeHeapBlock(span, slot_sz * 2);
        for (i = 0; i < NS; i++) {
            if (i == 63 || i == 64)
                continue;
            FreeHeapBlock(all[i], slot_sz);
        }
    }

    void *small = AllocHeapBlock(NULL, 4096, 1);
    if (small == NULL) {
        fprintf(stderr, "sub-64KB fallback VirtualAlloc failed\n");
        QForkShutdown();
        return 7;
    }
    if (!FreeHeapBlock(small, 4096)) {
        fprintf(stderr, "sub-64KB FreeHeapBlock failed\n");
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

    QForkShutdown();
    if (g_pQForkControl != NULL) {
        fprintf(stderr, "g_pQForkControl not cleared\n");
        return 8;
    }

    printf("ok qfork heap (NULL-safe, map, commit-in-place, recycle)\n");
    return 0;
}