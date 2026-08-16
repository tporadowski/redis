/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#include "server.h"
#include "rdb.h"
#include "Win32_QFork.h"
#include "Win32_QFork_impl.h"
#include "Win32_ThreadControl.h"
#include "Win32_ProcessTable.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <errno.h>
#include <stdio.h>
#endif

Win32QForkJob g_win32_qfork_job;

void win32PrepareRdbDiskJob(int req, const char *filename, const void *rsi, int rdbflags) {
    memset(&g_win32_qfork_job, 0, sizeof(g_win32_qfork_job));
    g_win32_qfork_job.rdb_req = req;
    g_win32_qfork_job.rdb_flags = rdbflags;
    if (filename)
        strncpy(g_win32_qfork_job.filename, filename, sizeof(g_win32_qfork_job.filename) - 1);
    if (rsi) {
        memcpy(g_win32_qfork_job.rsi, rsi, sizeof(rdbSaveInfo));
        g_win32_qfork_job.rsi_valid = 1;
    }
}

void SetupRedisGlobals(void *redisData, size_t redisDataSize, unsigned char *dictHashSeed) {
    if (redisData && redisDataSize == sizeof(server))
        memcpy(&server, redisData, redisDataSize);
    if (dictHashSeed)
        dictSetHashFunctionSeed(dictHashSeed);
    server.el = NULL;
    server.pid = (int)GetCurrentProcessId();
    server.main_thread_id = pthread_self();
    server.child_pid = -1;
    server.child_type = CHILD_TYPE_NONE;
    server.in_fork_child = CHILD_TYPE_RDB;
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

int win32RedisFork(int purpose) {
    if (purpose != CHILD_TYPE_RDB) {
        errno = ENOSYS;
        return -1;
    }
    if (!g_HasMemoryMappedHeap || g_BypassMemoryMapOnAlloc) {
        /* Parent-side RDB until jemalloc can live in the mapped heap. */
        serverLog(LL_NOTICE,
                  "QFork: mapped heap not live (bypass=%d); saving RDB in parent",
                  g_BypassMemoryMapOnAlloc);
        void *rsi = g_win32_qfork_job.rsi_valid ? (void *)g_win32_qfork_job.rsi
                                                : NULL;
        int rc = do_rdbSave(g_win32_qfork_job.rdb_req,
                            g_win32_qfork_job.filename, rsi,
                            g_win32_qfork_job.rdb_flags);
        char fileName[MAX_PATH];
        if (!GetModuleFileNameA(NULL, fileName, MAX_PATH)) {
            errno = EIO;
            return -1;
        }
        char arguments[MAX_PATH * 2];
        _snprintf_s(arguments, sizeof(arguments), _TRUNCATE,
                    "\"%s\" --QForkExit %d", fileName, rc == C_OK ? 0 : 1);
        STARTUPINFOA si;
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi;
        memset(&pi, 0, sizeof(pi));
        if (!CreateProcessA(fileName, arguments, NULL, NULL, FALSE, 0, NULL,
                            NULL, &si, &pi)) {
            serverLog(LL_WARNING, "QFork: dummy child CreateProcess gle=%lu",
                      GetLastError());
            errno = EAGAIN;
            return -1;
        }
        CloseHandle(pi.hThread);
        winpid_register((pid_t)pi.dwProcessId, pi.hProcess, WP_QFORK, NULL);
        return (int)pi.dwProcessId;
    }

    size_t payload_size = sizeof(QForkPayloadHeader) + sizeof(server);
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
    if (!QForkSpawnChild(hPayload, abort_ev, &child_pid, (void **)&hProcess)) {
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
