/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#include "server.h"
#include "rdb.h"
#include "Win32_QFork.h"
#include "Win32_QFork_impl.h"
#include "Win32_ThreadControl.h"
#include "Win32_ProcessTable.h"
#include "Win32_FDAPI.h"
#include "connection.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <errno.h>
#include <stdio.h>
#endif

Win32QForkJob g_win32_qfork_job;

int rewriteAppendOnlyFile(char *filename);
int rdbSaveRioWithEOFMark(int req, rio *rdb, int *error, rdbSaveInfo *rsi);
int slotSnapshotSaveRio(int req, rio *rdb, int *error);

void win32PrepareRdbDiskJob(int req, const char *filename, const void *rsi, int rdbflags) {
    memset(&g_win32_qfork_job, 0, sizeof(g_win32_qfork_job));
    g_win32_qfork_job.purpose = CHILD_TYPE_RDB;
    g_win32_qfork_job.rdb_req = req;
    g_win32_qfork_job.rdb_flags = rdbflags;
    if (filename)
        strncpy(g_win32_qfork_job.filename, filename, sizeof(g_win32_qfork_job.filename) - 1);
    if (rsi) {
        memcpy(g_win32_qfork_job.rsi, rsi, sizeof(rdbSaveInfo));
        g_win32_qfork_job.rsi_valid = 1;
    }
}

void win32PrepareAofJob(void) {
    memset(&g_win32_qfork_job, 0, sizeof(g_win32_qfork_job));
    g_win32_qfork_job.purpose = CHILD_TYPE_AOF;
}

void win32PrepareModuleJob(const char *path, const char *symbol, void *user_data) {
    memset(&g_win32_qfork_job, 0, sizeof(g_win32_qfork_job));
    g_win32_qfork_job.purpose = CHILD_TYPE_MODULE;
    if (path)
        strncpy(g_win32_qfork_job.filename, path, sizeof(g_win32_qfork_job.filename) - 1);
    if (symbol)
        strncpy(g_win32_qfork_job.module_symbol, symbol,
                sizeof(g_win32_qfork_job.module_symbol) - 1);
    g_win32_qfork_job.module_user_data = user_data;
}

int do_moduleFork(const char *path, const char *symbol, void *user_data) {
    HMODULE h;
    void (*fn)(void *);

    if (!symbol || !symbol[0])
        return C_ERR;
    if (path && path[0])
        h = LoadLibraryA(path);
    else
        h = GetModuleHandleA(NULL);
    if (!h) {
        fprintf(stderr, "do_moduleFork: LoadLibrary(%s) gle=%lu\n",
                path && path[0] ? path : "(exe)", GetLastError());
        return C_ERR;
    }
    fn = (void (*)(void *))(uintptr_t)GetProcAddress(h, symbol);
    if (!fn) {
        fprintf(stderr, "do_moduleFork: GetProcAddress(%s) gle=%lu\n",
                symbol, GetLastError());
        if (path && path[0])
            FreeLibrary(h);
        return C_ERR;
    }
    fn(user_data);
    if (path && path[0])
        FreeLibrary(h);
    return C_OK;
}

void win32PrepareRdbSocketJob(int req, const void *rsi, int rdb_channel,
                              int slots_req, void **conns, int numconns,
                              int rdb_pipe_write, int safe_to_exit_pipe) {
    memset(&g_win32_qfork_job, 0, sizeof(g_win32_qfork_job));
    g_win32_qfork_job.purpose = CHILD_TYPE_RDB;
    g_win32_qfork_job.rdb_subtype = rdb_channel ? QFORK_RDB_CHANNEL
                                               : QFORK_RDB_SOCKET_PIPE;
    g_win32_qfork_job.rdb_req = req;
    g_win32_qfork_job.rdb_channel = rdb_channel;
    g_win32_qfork_job.slots_req = slots_req;
    g_win32_qfork_job.numconns = numconns;
    g_win32_qfork_job.conns = conns;
    g_win32_qfork_job.rdb_pipe_write = rdb_pipe_write;
    g_win32_qfork_job.safe_to_exit_pipe = safe_to_exit_pipe;
    if (rsi) {
        memcpy(g_win32_qfork_job.rsi, rsi, sizeof(rdbSaveInfo));
        g_win32_qfork_job.rsi_valid = 1;
    }
}

void SetupRedisGlobals(void *redisData, size_t redisDataSize,
                       unsigned char *dictHashSeed, int purpose) {
    if (redisData && redisDataSize == sizeof(server))
        memcpy(&server, redisData, redisDataSize);
    if (dictHashSeed)
        dictSetHashFunctionSeed(dictHashSeed);
    server.el = NULL;
    server.pid = (int)GetCurrentProcessId();
    server.main_thread_id = pthread_self();
    server.child_pid = -1;
    server.child_type = CHILD_TYPE_NONE;
    server.in_fork_child = purpose;
    server.child_info_pipe[0] = -1;
    server.child_info_pipe[1] = -1;
    server.module_pipe[0] = -1;
    server.module_pipe[1] = -1;
}

int do_rdbSave(int req, char *filename, void *rsi, int rdbflags) {
    rdbSaveInfo *rsiptr = rsi ? (rdbSaveInfo *)rsi : NULL;
    if (rdbSave(req, filename, rsiptr, rdbflags) != C_OK) {
        serverLog(LL_WARNING, "rdbSave failed in qfork: %s", strerror(errno));
        return C_ERR;
    }
    sendChildCowInfo(CHILD_INFO_TYPE_RDB_COW_SIZE, "RDB");
    return C_OK;
}

int do_rdbSaveToSockets(int req, void *rsi, void **conns, int numconns,
                        int use_conns, int rdb_pipe_write, int safe_to_exit) {
    rio rdb;
    int retval;
    rdbSaveInfo *rsiptr = rsi ? (rdbSaveInfo *)rsi : NULL;
    int dummy;

    if (req & SLAVE_REQ_RDB_NO_COMPRESS)
        server.rdb_compression = 0;
    if (req & SLAVE_REQ_RDB_NO_CHECKSUM)
        server.rdb_checksum = 0;

    if (use_conns) {
        rioInitWithConnset(&rdb, (connection **)conns, (size_t)numconns);
    } else {
        rioInitWithFd(&rdb, rdb_pipe_write);
    }

    if (req & SLAVE_REQ_SLOTS_SNAPSHOT)
        retval = slotSnapshotSaveRio(req, &rdb, NULL);
    else
        retval = rdbSaveRioWithEOFMark(req, &rdb, NULL, rsiptr);

    if (retval == C_OK && rioFlush(&rdb) == 0)
        retval = C_ERR;
    if (retval == C_OK)
        sendChildCowInfo(CHILD_INFO_TYPE_RDB_COW_SIZE, "RDB");

    if (use_conns) {
        rioFreeConnset(&rdb);
    } else {
        rioFreeFd(&rdb);
        if (rdb_pipe_write >= 0)
            close(rdb_pipe_write);
        if (safe_to_exit >= 0) {
            dummy = (int)read(safe_to_exit, &dummy, 1);
            UNUSED(dummy);
        }
    }
    return retval;
}

int do_rdbSaveToSocketsChild(QForkPayloadHeader *hdr, void *proto_blob) {
    connection *local[QFORK_MAX_SOCKET_CONNS];
    int i, n;

    if (!hdr || hdr->numconns <= 0)
        return C_ERR;
    n = hdr->numconns;
    if (n > QFORK_MAX_SOCKET_CONNS) n = QFORK_MAX_SOCKET_CONNS;
    connTypeInitialize();
    for (i = 0; i < n; i++) {
        int rfd = FDAPI_WSASocketFromInfo(
            (char *)proto_blob + (size_t)i * QFORK_PROTO_INFO_SIZE);
        connection *c;
        if (rfd < 0)
            return C_ERR;
        c = connectionTypeTcp()->conn_create_accepted(NULL, rfd, NULL);
        if (!c)
            return C_ERR;
        c->state = CONN_STATE_CONNECTED;
        connBlock(c);
        local[i] = c;
    }
    return do_rdbSaveToSockets(hdr->rdb_req,
                               hdr->rsi_valid ? (void *)hdr->rsi : NULL,
                               (void **)local, n, 1, -1, -1);
}

int do_aofRewrite(const char *filename) {
    char tmpfile[256];
    if (!filename) {
        snprintf(tmpfile, sizeof(tmpfile), "temp-rewriteaof-bg-%d.aof",
                 (int)getpid());
        filename = tmpfile;
    }
    if (rewriteAppendOnlyFile((char *)filename) != C_OK) {
        serverLog(LL_WARNING, "rewriteAppendOnlyFile failed in qfork: %s",
                  strerror(errno));
        return C_ERR;
    }
    sendChildCowInfo(CHILD_INFO_TYPE_AOF_COW_SIZE, "AOF rewrite");
    return C_OK;
}

void win32ApplyPersistenceAvailable(int available) {
    if (available)
        return;
    const char *drop[] = {
        "bgsave", "bgrewriteaof", "replconf", "psync", "sync", "backup", NULL
    };
    for (int i = 0; drop[i]; i++) {
        struct redisCommand *cmd = lookupCommandByCString(drop[i]);
        if (!cmd)
            continue;
        if (server.commands)
            dictDelete(server.commands, cmd->fullname);
        if (server.orig_commands)
            dictDelete(server.orig_commands, cmd->fullname);
    }
}

static int spawn_qfork_exit(int exit_code, DWORD flags, DWORD *pid,
                            HANDLE *proc, HANDLE *thread) {
    char fileName[MAX_PATH];
    if (!GetModuleFileNameA(NULL, fileName, MAX_PATH))
        return 0;
    char arguments[MAX_PATH * 2];
    _snprintf_s(arguments, sizeof(arguments), _TRUNCATE,
                "\"%s\" --QForkExit %d", fileName, exit_code);
    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessA(fileName, arguments, NULL, NULL, FALSE, flags, NULL,
                        NULL, &si, &pi)) {
        serverLog(LL_WARNING, "QFork: dummy child CreateProcess gle=%lu",
                  GetLastError());
        return 0;
    }
    *pid = pi.dwProcessId;
    *proc = pi.hProcess;
    if (thread)
        *thread = pi.hThread;
    else
        CloseHandle(pi.hThread);
    return 1;
}

static int win32ParentSideFork(int purpose) {
    const char *what = "saving RDB";
    if (purpose == CHILD_TYPE_AOF)
        what = "rewriting AOF";
    else if (g_win32_qfork_job.rdb_subtype != QFORK_RDB_DISK)
        what = "diskless RDB to sockets";
    serverLog(LL_NOTICE,
              "QFork: mapped heap not live (bypass=%d); %s in parent",
              g_BypassMemoryMapOnAlloc, what);

    if (purpose == CHILD_TYPE_RDB &&
        g_win32_qfork_job.rdb_subtype != QFORK_RDB_DISK) {
        int i;
        void *rsi = g_win32_qfork_job.rsi_valid ? (void *)g_win32_qfork_job.rsi
                                                : NULL;
        for (i = 0; i < g_win32_qfork_job.numconns; i++) {
            connection *c = (connection *)g_win32_qfork_job.conns[i];
            if (!c) continue;
            connSendTimeout(c, (long long)server.repl_timeout * 1000);
            connBlock(c);
        }
        int rc = do_rdbSaveToSockets(g_win32_qfork_job.rdb_req, rsi,
                                     g_win32_qfork_job.conns,
                                     g_win32_qfork_job.numconns,
                                     1, -1, -1);
        DWORD pid = 0;
        HANDLE proc = NULL;
        if (!spawn_qfork_exit(rc == C_OK ? 0 : 1, 0, &pid, &proc, NULL)) {
            errno = EAGAIN;
            return -1;
        }
        winpid_register((pid_t)pid, proc, WP_QFORK, NULL);
        return (int)pid;
    }

    if (purpose == CHILD_TYPE_AOF) {
        /* Parent later looks for temp-rewriteaof-bg-<child_pid>.aof.
         * Spawn suspended so we know the pid before writing. */
        DWORD pid = 0;
        HANDLE proc = NULL, thr = NULL;
        if (!spawn_qfork_exit(0, CREATE_SUSPENDED, &pid, &proc, &thr)) {
            errno = EAGAIN;
            return -1;
        }
        char tmpfile[256];
        snprintf(tmpfile, sizeof(tmpfile), "temp-rewriteaof-bg-%d.aof",
                 (int)pid);
        int rc = do_aofRewrite(tmpfile);
        if (rc != C_OK) {
            TerminateProcess(proc, 1);
            CloseHandle(thr);
            CloseHandle(proc);
            errno = EIO;
            return -1;
        }
        ResumeThread(thr);
        CloseHandle(thr);
        winpid_register((pid_t)pid, proc, WP_QFORK, NULL);
        return (int)pid;
    }

    void *rsi = g_win32_qfork_job.rsi_valid ? (void *)g_win32_qfork_job.rsi
                                            : NULL;
    int rc = do_rdbSave(g_win32_qfork_job.rdb_req,
                        g_win32_qfork_job.filename, rsi,
                        g_win32_qfork_job.rdb_flags);
    DWORD pid = 0;
    HANDLE proc = NULL;
    if (!spawn_qfork_exit(rc == C_OK ? 0 : 1, 0, &pid, &proc, NULL)) {
        errno = EAGAIN;
        return -1;
    }
    winpid_register((pid_t)pid, proc, WP_QFORK, NULL);
    return (int)pid;
}

int win32RedisFork(int purpose) {
    if (purpose == CHILD_TYPE_MODULE) {
        /* Always spawn a --QFork child; never return 0 in this process. */
        if (g_win32_qfork_job.purpose != CHILD_TYPE_MODULE) {
            errno = EINVAL;
            return -1;
        }
        /* Fall through to payload + CreateProcess below. */
    } else if (purpose != CHILD_TYPE_RDB && purpose != CHILD_TYPE_AOF) {
        errno = ENOSYS;
        return -1;
    } else if (!g_HasMemoryMappedHeap || g_BypassMemoryMapOnAlloc) {
        return win32ParentSideFork(purpose);
    }

    int nconns = g_win32_qfork_job.numconns;
    if (nconns < 0) nconns = 0;
    if (nconns > QFORK_MAX_SOCKET_CONNS) nconns = QFORK_MAX_SOCKET_CONNS;
    int socket_job = (purpose == CHILD_TYPE_RDB &&
                      g_win32_qfork_job.rdb_subtype != QFORK_RDB_DISK);
    size_t payload_size = sizeof(QForkPayloadHeader) + sizeof(server) +
                          (socket_job ? (size_t)nconns * QFORK_PROTO_INFO_SIZE : 0);
    HANDLE hPayload = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL,
                                         PAGE_READWRITE, 0,
                                         (DWORD)payload_size, NULL);
    if (!hPayload) {
        serverLog(LL_WARNING, "QFork: CreateFileMapping payload gle=%lu",
                  GetLastError());
        errno = ENOMEM;
        return -1;
    }

    QForkPayloadHeader *hdr = (QForkPayloadHeader *)MapViewOfFile(
        hPayload, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!hdr) {
        serverLog(LL_WARNING, "QFork: MapViewOfFile payload gle=%lu",
                  GetLastError());
        CloseHandle(hPayload);
        errno = ENOMEM;
        return -1;
    }
    memset(hdr, 0, payload_size);
    hdr->magic = QFORK_MAGIC;
    hdr->purpose = (uint32_t)purpose;
    hdr->redisDataSize = sizeof(server);
    dictGetHashFunctionSeed(hdr->dictHashSeed);
    hdr->parent_pid = GetCurrentProcessId();
    hdr->rdb_req = g_win32_qfork_job.rdb_req;
    hdr->rdb_flags = g_win32_qfork_job.rdb_flags;
    hdr->rsi_valid = g_win32_qfork_job.rsi_valid;
    memcpy(hdr->filename, g_win32_qfork_job.filename, sizeof(hdr->filename));
    if (g_win32_qfork_job.rsi_valid)
        memcpy(hdr->rsi, g_win32_qfork_job.rsi, sizeof(hdr->rsi));
    hdr->rdb_subtype = g_win32_qfork_job.rdb_subtype;
    hdr->rdb_channel = g_win32_qfork_job.rdb_channel;
    hdr->slots_req = g_win32_qfork_job.slots_req;
    hdr->numconns = nconns;
    memcpy(hdr->module_symbol, g_win32_qfork_job.module_symbol,
           sizeof(hdr->module_symbol));
    hdr->module_user_data = (uint64_t)(uintptr_t)g_win32_qfork_job.module_user_data;
    memcpy(hdr + 1, &server, sizeof(server));

    HANDLE abort_ev = CreateEvent(NULL, TRUE, FALSE, NULL);

    pauseAllIOThreads();
    RequestSuspension();
    DWORD waited = 0;
    while (!SuspensionCompleted() && waited < 1000) {
        Sleep(1);
        waited++;
    }

    if (!QForkProtectForFork()) {
        serverLog(LL_WARNING, "QFork: PAGE_WRITECOPY failed gle=%lu",
                  GetLastError());
        ResumeFromSuspension();
        resumeAllIOThreads();
        UnmapViewOfFile(hdr);
        CloseHandle(hPayload);
        if (abort_ev)
            CloseHandle(abort_ev);
        errno = EIO;
        return -1;
    }

    unsigned long child_pid = 0;
    HANDLE hProcess = NULL;
    HANDLE hThread = NULL;
    if (!QForkSpawnChild(hPayload, abort_ev, &child_pid, (void **)&hProcess,
                         socket_job, socket_job ? (void **)&hThread : NULL)) {
        QForkRejoinAfterFork();
        ResumeFromSuspension();
        resumeAllIOThreads();
        UnmapViewOfFile(hdr);
        CloseHandle(hPayload);
        if (abort_ev)
            CloseHandle(abort_ev);
        errno = EAGAIN;
        return -1;
    }

    if (socket_job && hThread) {
        unsigned char *proto = (unsigned char *)(hdr + 1) + sizeof(server);
        int i;
        for (i = 0; i < nconns; i++) {
            connection *c = (connection *)g_win32_qfork_job.conns[i];
            if (!c || FDAPI_WSADuplicateSocket(c->fd, child_pid,
                                               proto + (size_t)i * QFORK_PROTO_INFO_SIZE) != 0) {
                serverLog(LL_WARNING, "QFork: WSADuplicateSocket conn %d failed", i);
                TerminateProcess(hProcess, 1);
                CloseHandle(hThread);
                CloseHandle(hProcess);
                QForkRejoinAfterFork();
                ResumeFromSuspension();
                resumeAllIOThreads();
                UnmapViewOfFile(hdr);
                CloseHandle(hPayload);
                if (abort_ev) CloseHandle(abort_ev);
                errno = EIO;
                return -1;
            }
        }
        ResumeThread(hThread);
        CloseHandle(hThread);
    }

    ResumeFromSuspension();
    resumeAllIOThreads();

    if (!QForkRejoinAfterFork()) {
        serverLog(LL_WARNING, "QFork: rejoin after CreateProcess failed");
        TerminateProcess(hProcess, 1);
        CloseHandle(hProcess);
        UnmapViewOfFile(hdr);
        CloseHandle(hPayload);
        if (abort_ev)
            CloseHandle(abort_ev);
        errno = EIO;
        return -1;
    }

    UnmapViewOfFile(hdr);
    /* Mapping object stays open in the child via DuplicateHandle; drop our view
     * but keep the handle until waitpid reaps (child dups immediately). */
    CloseHandle(hPayload);

    winpid_register((pid_t)child_pid, hProcess, WP_QFORK, abort_ev);
    serverLog(LL_NOTICE, "QFork: child pid %lu created (%lu us freeze+spawn)",
              child_pid, (unsigned long)waited * 1000);
    return (int)child_pid;
}
