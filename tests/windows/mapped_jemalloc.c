/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/*
 * 11.1: jemalloc on the QFork mapped heap (g_BypassMemoryMapOnAlloc=0).
 * Prints every AllocHeapBlock via QFORK_HEAP_LOG in Win32_QFork_heap.cpp.
 */
#include <stdio.h>
#include <string.h>
#include <jemalloc/jemalloc.h>
#include "Win32_QFork.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

const char *je_malloc_conf = "narenas:1,tcache_nslots_small_max:8";

#define BLOCK ((size_t)1 << 22)

int main(int argc, char **argv) {
    /* argv[1]=="full" → same 10× RAM heap as redis-server (the 16-byte OOM case). */
    size_t init_bytes = BLOCK * 64;
    if (argc > 1 && strcmp(argv[1], "full") == 0)
        init_bytes = 0;
    if (!QForkParentInit(init_bytes)) {
        fprintf(stderr, "QForkParentInit failed\n");
        return 2;
    }
    if (!g_HasMemoryMappedHeap || g_BypassMemoryMapOnAlloc) {
        fprintf(stderr, "heap not live after init (mapped=%d bypass=%d)\n",
                g_HasMemoryMappedHeap, g_BypassMemoryMapOnAlloc);
        QForkShutdown();
        return 2;
    }

    fprintf(stderr, "heap live control=%p bypass=%d\n",
            g_pQForkControl, g_BypassMemoryMapOnAlloc);

    void *first = je_malloc(16);
    if (first == NULL) {
        fprintf(stderr, "FAIL first je_malloc(16)\n");
        QForkShutdown();
        return 3;
    }
    memset(first, 0xAB, 16);
    fprintf(stderr, "first je_malloc(16)=%p usable=%zu\n",
            first, je_malloc_usable_size(first));

    int i;
    for (i = 0; i < 10000; i++) {
        void *p = je_malloc(16);
        if (p == NULL) {
            fprintf(stderr, "FAIL je_malloc(16) at i=%d\n", i);
            QForkShutdown();
            return 4;
        }
        ((unsigned char *)p)[0] = (unsigned char)i;
    }
    fprintf(stderr, "ok 10000 additional 16-byte allocs\n");

    /* Mix sizes like initServer / kvstore (dicts, SDS, etc.). */
    void *mix[256];
    size_t sizes[] = {8, 16, 24, 32, 48, 64, 128, 256, 512, 1024, 4096,
                      16384, 65536, 262144, 1048576};
    int n = 0;
    for (i = 0; i < 256; i++) {
        size_t sz = sizes[i % (int)(sizeof(sizes) / sizeof(sizes[0]))];
        mix[i] = je_malloc(sz);
        if (mix[i] == NULL) {
            fprintf(stderr, "FAIL je_malloc(%zu) at mix i=%d\n", sz, i);
            QForkShutdown();
            return 5;
        }
        memset(mix[i], 0xCD, sz < 64 ? sz : 64);
        n++;
    }
    fprintf(stderr, "ok %d mixed-size allocs\n", n);

    /* 16 kvstore-ish bursts: several hundred small objects each. */
    for (int db = 0; db < 16; db++) {
        for (i = 0; i < 200; i++) {
            void *p = je_calloc(1, 16);
            if (p == NULL) {
                fprintf(stderr, "FAIL je_calloc(16) db=%d i=%d\n", db, i);
                QForkShutdown();
                return 6;
            }
        }
    }
    fprintf(stderr, "ok 16x200 calloc(16) after mixed allocs\n");

    je_free(first);
    for (i = 0; i < 256; i++)
        je_free(mix[i]);

    QForkShutdown();
    printf("ok mapped jemalloc (16-byte + mixed + kvstore-ish)\n");
    return 0;
}
