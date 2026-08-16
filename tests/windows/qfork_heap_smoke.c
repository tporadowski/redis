/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#include <stdio.h>
#include <string.h>
#include "Win32_QFork.h"

#define BLOCK ((size_t)1 << 22)

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

    void *b = AllocHeapBlock(NULL, BLOCK, 1);
    if (b != a) {
        fprintf(stderr, "expected recycle of first block %p got %p\n", a, b);
        QForkShutdown();
        return 6;
    }
    FreeHeapBlock(b, BLOCK);

    void *small = AllocHeapBlock(NULL, 4096, 1);
    if (small == NULL) {
        fprintf(stderr, "sub-4MB fallback VirtualAlloc failed\n");
        QForkShutdown();
        return 7;
    }
    if (!FreeHeapBlock(small, 4096)) {
        fprintf(stderr, "sub-4MB FreeHeapBlock failed\n");
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