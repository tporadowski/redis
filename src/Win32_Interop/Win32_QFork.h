/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_INTEROP_QFORK_H
#define WIN32_INTEROP_QFORK_H

#ifdef _WIN32

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int redis_main(int argc, char **argv);

/*
 * jemalloc page hooks. AllocHeapBlock is NULL-safe: if g_pQForkControl is
 * NULL (pages_boot before QForkStartup, check tools, sentinel) it never
 * dereferences the map — VirtualAlloc fallback.
 *
 * No MAX_REDIS_DATA_SIZE. Fork payload is a separate mapped section (3.3).
 */
extern void *g_pQForkControl;
extern int g_BypassMemoryMapOnAlloc;
extern int g_HasMemoryMappedHeap;

/* heap_bytes == 0 → default (10× physical RAM, cap 1 TB). */
int QForkParentInit(size_t heap_bytes);
void QForkShutdown(void);

void *AllocHeapBlock(void *addr, size_t size, int zero);
int FreeHeapBlock(void *addr, size_t size);
int PurgePages(void *addr, size_t length);
int CommitHeapBlock(void *addr, size_t size, int commit);

/* Pre-parse result: 1 if persistence-available no (argv or conf). */
extern int g_PersistenceDisabled;

#define QFORK_MAGIC 0x51463130u /* 'QF10' */

typedef struct QForkPayloadHeader {
    uint32_t magic;
    uint32_t purpose;
    size_t   redisDataSize;
    uint8_t  dictHashSeed[16];
    unsigned long parent_pid;
    /* RDB disk */
    int      rdb_req;
    int      rdb_flags;
    int      rsi_valid;
    char     filename[260];
    unsigned char rsi[80];
} QForkPayloadHeader;

void win32PrepareRdbDiskJob(int req, const char *filename, const void *rsi, int rdbflags);
int win32RedisFork(int purpose);
void win32ApplyPersistenceAvailable(int available);
int QForkChildMain(void *control_handle, void *payload_handle, unsigned long parent_pid);
int QForkPreparsePersistence(int argc, char **argv);

/* Parent: create the --QFork child. process_out receives an owned HANDLE. */
int QForkSpawnChild(void *payload_map, void *abort_event,
                    unsigned long *pid_out, void **process_out);

void *QForkGetControlMap(void);
int QForkProtectForFork(void);
int QForkChildAttach(void *parent_process, void *control_map);
int QForkRejoinAfterFork(void);

#ifndef QFORK_MAIN_IMPL
#define main redis_main
#endif

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */
#endif
