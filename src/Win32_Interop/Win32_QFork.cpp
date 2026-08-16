/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/*
 * M1 entry: CRT main → redis_main.
 * 3.1: jemalloc page-hook fallbacks (VirtualAlloc). No QFork heap yet.
 * 3.2 installs QForkParentInit / child dispatch and the mapped heap.
 */
#define QFORK_MAIN_IMPL
#include "Win32_QFork.h"
#include "Win32_Time.h"
#include "Win32_FDAPI.h"
#include "Win32_ThreadControl.h"

int main(int argc, char **argv) {
    InitTimeFunctions();
    InitThreadControl();
    FDAPI_Init();
    return redis_main(argc, argv);
}
