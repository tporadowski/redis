/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/*
 * CRT main: QFork heap then redis_main. No je_malloc before QForkStartup.
 * --QFork child dispatch is 3.3.
 */
#define QFORK_MAIN_IMPL
#include "Win32_QFork.h"
#include "Win32_Time.h"
#include "Win32_FDAPI.h"
#include "Win32_ThreadControl.h"

#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
#endif
int checkForSentinelMode(int argc, char **argv, char *exec_name);

static int argv_has_qfork(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--QFork") == 0)
            return 1;
    }
    return 0;
}

static int argv_persistence_off(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--persistence-available") == 0 ||
            strcmp(argv[i], "persistence-available") == 0) {
            if (i + 1 < argc && _stricmp(argv[i + 1], "no") == 0)
                return 1;
        }
    }
    return 0;
}

static int is_check_tool(const char *argv0) {
    if (!argv0)
        return 0;
    const char *base = argv0;
    for (const char *p = argv0; *p; p++) {
        if (*p == '\\' || *p == '/')
            base = p + 1;
    }
    return _stricmp(base, "redis-check-rdb.exe") == 0 ||
           _stricmp(base, "redis-check-rdb") == 0 ||
           _stricmp(base, "redis-check-aof.exe") == 0 ||
           _stricmp(base, "redis-check-aof") == 0 ||
           strstr(base, "redis-check-rdb") != NULL ||
           strstr(base, "redis-check-aof") != NULL;
}

int main(int argc, char **argv) {
    InitTimeFunctions();
    InitThreadControl();
    FDAPI_Init();

    if (argv_has_qfork(argc, argv)) {
        fprintf(stderr, "QFork child dispatch is not implemented yet (3.3).\n");
        return 1;
    }

    int skip_heap = is_check_tool(argv[0]) ||
                    checkForSentinelMode(argc, argv, argv[0]) ||
                    argv_persistence_off(argc, argv);
    if (skip_heap) {
        g_BypassMemoryMapOnAlloc = 1;
        return redis_main(argc, argv);
    }

    if (!QForkParentInit(0))
        return 1;

    /*
     * The pagefile heap is reserved and AllocHeapBlock can serve 4 MB maps.
     * jemalloc 5.3 still OOMs (16-byte) during db init when those maps back
     * the allocator (bypass=0). Keep VirtualAlloc for zmalloc until 3.3
     * COW requires the mapped heap to be live. pages_boot stays NULL-safe.
     */
    g_BypassMemoryMapOnAlloc = 1;

    int rc = redis_main(argc, argv);
    QForkShutdown();
    return rc;
}
