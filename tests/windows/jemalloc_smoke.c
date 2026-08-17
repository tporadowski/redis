/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#include <stdio.h>
#include <string.h>
#include <jemalloc/jemalloc.h>
#include "Win32_QFork.h"

/* Windows has no weak je_malloc_conf; embedders must define it. */
const char *je_malloc_conf = "lg_tcache_nslots_mul:3,tcache_nslots_small_max:1000";

int main(void) {
    void *page = AllocHeapBlock(NULL, (size_t)1 << 22, 1);
    if (page == NULL) {
        fprintf(stderr, "AllocHeapBlock 4MB failed\n");
        return 1;
    }
    memset(page, 0xAB, 64);
    if (!FreeHeapBlock(page, (size_t)1 << 22)) {
        fprintf(stderr, "FreeHeapBlock failed\n");
        return 1;
    }

    size_t usize = 0;
    void *p = je_malloc_with_usize(8, &usize);
    if (p == NULL) {
        fprintf(stderr, "je_malloc_with_usize(8) failed\n");
        return 2;
    }
    printf("je_malloc_with_usize(8) -> %p usize=%zu\n", p, usize);
    memset(p, 0xCD, 8);
    je_free(p);

    p = je_malloc(8);
    if (p == NULL) {
        fprintf(stderr, "je_malloc(8) failed\n");
        return 3;
    }
    printf("je_malloc(8) -> %p usable=%zu\n", p, je_malloc_usable_size(p));
    je_free(p);

    const char *ver = JEMALLOC_VERSION;
    printf("ok jemalloc %s\n", ver);
    return 0;
}
