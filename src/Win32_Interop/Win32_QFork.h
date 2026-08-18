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
/* Parent console/service path: QFork heap then redis_main. */
int RedisWindowsParentMain(int argc, char **argv);

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

/* heap_bytes == 0 → QFORK_HEAP_BYTES env (M/G suffix ok), else 10× RAM, cap 1 TB. */
int QForkParentInit(size_t heap_bytes);
void QForkShutdown(void);

void *AllocHeapBlock(void *addr, size_t size, int zero);
int FreeHeapBlock(void *addr, size_t size);
int PurgePages(void *addr, size_t length);
int CommitHeapBlock(void *addr, size_t size, int commit);

/* Pre-parse result: 1 if persistence-available no (argv or conf). */
extern int g_PersistenceDisabled;

#define QFORK_MAGIC 0x51463130u /* 'QF10' */

#ifndef CHILD_TYPE_RDB
#define CHILD_TYPE_RDB 1
#define CHILD_TYPE_AOF 2
#define CHILD_TYPE_LDB 3
#define CHILD_TYPE_MODULE 4
#endif

#define QFORK_RDB_DISK         0
#define QFORK_RDB_SOCKET_PIPE  1
#define QFORK_RDB_CHANNEL      2
#define QFORK_MAX_SOCKET_CONNS 128
#define QFORK_PROTO_INFO_SIZE  512 /* >= sizeof(WSAPROTOCOL_INFO) */

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
    /* RDB socket / rdb-channel (4.2) */
    int      rdb_subtype;
    int      rdb_channel;
    int      slots_req;
    int      numconns;
    /* CHILD_TYPE_MODULE: exported symbol + user_data (pointer, COW if live). */
    char     module_symbol[64];
    uint64_t module_user_data;
} QForkPayloadHeader;

void win32PrepareRdbDiskJob(int req, const char *filename, const void *rsi, int rdbflags);
void win32PrepareRdbSocketJob(int req, const void *rsi, int rdb_channel,
                              int slots_req, void **conns, int numconns,
                              int rdb_pipe_write, int safe_to_exit_pipe);
void win32PrepareAofJob(void);
void win32PrepareModuleJob(const char *path, const char *symbol, void *user_data);
int win32RedisFork(int purpose);
void win32ApplyPersistenceAvailable(int available);
int QForkChildMain(void *control_handle, void *payload_handle, unsigned long parent_pid);
int QForkPreparsePersistence(int argc, char **argv);

/* Parent: create the --QFork child. process_out receives an owned HANDLE. */
int QForkSpawnChild(void *payload_map, void *abort_event,
                    unsigned long *pid_out, void **process_out,
                    int suspended, void **thread_out);

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
