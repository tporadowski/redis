/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/* Win64 LLP64 width smoke. pwin32 llp64.c adapted: no PORT_LONG, no server.h. */
#include "dict.h"

#include <stdint.h>
#include <stdio.h>

_Static_assert(sizeof(unsigned long) == 4,
               "This smoke must run against the Win64 LLP64 ABI");
_Static_assert(sizeof(size_t) == sizeof(uint64_t),
               "Win64 string and bit-operation lengths must be 64-bit");
_Static_assert(sizeof(void *) == sizeof(uint64_t),
               "Win64 pointers must be 64-bit");
_Static_assert(sizeof(uintptr_t) == sizeof(uint64_t),
               "Win64 pointer-sized integers must be 64-bit");
_Static_assert(sizeof(off_t) == sizeof(int64_t),
               "Win64 file offsets must be 64-bit");

int main(void) {
    const uint64_t high = (UINT64_C(1) << 32) + 17;
    volatile size_t size_high = high;
    volatile uintptr_t ptr_high = (uintptr_t)high;
    volatile off_t off_high = (off_t)high;

    if (DICTHT_SIZE(33) != (UINT64_C(1) << 33)) {
        fprintf(stderr, "FAIL: DICTHT_SIZE(33) = %llu, expected %llu\n",
                (unsigned long long)DICTHT_SIZE(33),
                (unsigned long long)(UINT64_C(1) << 33));
        return 1;
    }
    if (dictNextExpForSize((UINT64_C(1) << 32) + 1) != 33) {
        fprintf(stderr, "FAIL: dictNextExpForSize(2^32+1) = %d, expected 33\n",
                (int)dictNextExpForSize((UINT64_C(1) << 32) + 1));
        return 1;
    }
    if (dictNextExpForSize(DICT_HT_INITIAL_SIZE) != DICT_HT_INITIAL_EXP) {
        fprintf(stderr, "FAIL: dictNextExpForSize(initial) = %d\n",
                (int)dictNextExpForSize(DICT_HT_INITIAL_SIZE));
        return 1;
    }
    if (size_high != high) {
        fprintf(stderr, "FAIL: size_t truncated a value above 2^32\n");
        return 1;
    }
    if (ptr_high != (uintptr_t)high) {
        fprintf(stderr, "FAIL: uintptr_t truncated a value above 2^32\n");
        return 1;
    }
    if (off_high != (off_t)high) {
        fprintf(stderr, "FAIL: off_t truncated a value above 2^32\n");
        return 1;
    }

    printf("llp64_smoke: ok (ABI, DICTHT_SIZE(33), dictNextExpForSize)\n");
    return 0;
}
