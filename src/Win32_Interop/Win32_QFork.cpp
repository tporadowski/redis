/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/*
 * CRT main: QFork heap then redis_main, or --QFork child dispatch (3.3).
 */
#define QFORK_MAIN_IMPL
#include "Win32_QFork.h"
#include "Win32_QFork_impl.h"
#include "Win32_Time.h"
#include "Win32_FDAPI.h"
#include "Win32_ThreadControl.h"
#include "Win32_ProcessTable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifdef __cplusplus
extern "C"
#endif
int checkForSentinelMode(int argc, char **argv, char *exec_name);

#ifdef __cplusplus
extern "C"
#endif
void crc64_init(void);

static int argv_has_qfork(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--QFork") == 0)
            return i;
    }
    return 0;
}

static int token_is_no(const char *s) {
    return s && _stricmp(s, "no") == 0;
}

static int line_persistence_off(const char *line) {
    while (*line == ' ' || *line == '\t')
        line++;
    if (*line == '#' || *line == '\0' || *line == '\r' || *line == '\n')
        return 0;
    const char *key = "persistence-available";
    size_t n = strlen(key);
    if (_strnicmp(line, key, n) != 0)
        return 0;
    line += n;
    if (*line != ' ' && *line != '\t')
        return 0;
    while (*line == ' ' || *line == '\t')
        line++;
    return token_is_no(line);
}

static int scan_conf_persistence_off(const char *path, int depth) {
    if (!path || depth > 8)
        return 0;
    FILE *fp = fopen(path, "r");
    if (!fp)
        return 0;
    char buf[4096];
    int off = 0;
    while (fgets(buf, sizeof(buf), fp)) {
        char *p = buf;
        while (*p == ' ' || *p == '\t')
            p++;
        if (line_persistence_off(p)) {
            off = 1;
            break;
        }
        if (_strnicmp(p, "include", 7) == 0 && (p[7] == ' ' || p[7] == '\t')) {
            p += 7;
            while (*p == ' ' || *p == '\t')
                p++;
            char inc[MAX_PATH];
            int i = 0;
            if (*p == '"') {
                p++;
                while (*p && *p != '"' && i < MAX_PATH - 1)
                    inc[i++] = *p++;
            } else {
                while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' &&
                       i < MAX_PATH - 1)
                    inc[i++] = *p++;
            }
            inc[i] = '\0';
            if (inc[0] && scan_conf_persistence_off(inc, depth + 1)) {
                off = 1;
                break;
            }
        }
    }
    fclose(fp);
    return off;
}

int QForkPreparsePersistence(int argc, char **argv) {
    const char *conf = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--QFork") == 0) {
            i += 3;
            continue;
        }
        if (strcmp(argv[i], "--persistence-available") == 0 ||
            strcmp(argv[i], "persistence-available") == 0) {
            if (i + 1 < argc && token_is_no(argv[i + 1]))
                return 1;
            if (i + 1 < argc)
                i++;
            continue;
        }
        if (argv[i][0] == '-') {
            /* Skip --option value pairs so the next positional can be the conf. */
            if (argv[i][1] == '-' && i + 1 < argc && argv[i + 1][0] != '-')
                i++;
            continue;
        }
        if (!conf)
            conf = argv[i];
    }
    if (conf)
        return scan_conf_persistence_off(conf, 0);
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

int QForkSpawnChild(void *payload_map, void *abort_event,
                    unsigned long *pid_out, void **process_out) {
    (void)abort_event;
    char fileName[MAX_PATH];
    if (!GetModuleFileNameA(NULL, fileName, MAX_PATH)) {
        fprintf(stderr, "QForkSpawnChild: GetModuleFileName failed gle=%lu\n",
                GetLastError());
        return 0;
    }

    char arguments[MAX_PATH * 2];
    _snprintf_s(arguments, sizeof(arguments), _TRUNCATE,
                "\"%s\" --QFork %llu %llu %lu",
                fileName,
                (unsigned long long)(uintptr_t)QForkGetControlMap(),
                (unsigned long long)(uintptr_t)payload_map,
                (unsigned long)GetCurrentProcessId());

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcessA(fileName, arguments, NULL, NULL, TRUE, 0, NULL, NULL,
                        &si, &pi)) {
        fprintf(stderr, "QForkSpawnChild: CreateProcess failed gle=%lu\n",
                GetLastError());
        return 0;
    }
    CloseHandle(pi.hThread);
    if (pid_out)
        *pid_out = pi.dwProcessId;
    if (process_out)
        *process_out = (void *)pi.hProcess;
    else
        CloseHandle(pi.hProcess);
    return 1;
}

int QForkChildMain(void *control_handle, void *payload_handle,
                   unsigned long parent_pid) {
    HANDLE parent = OpenProcess(SYNCHRONIZE | PROCESS_DUP_HANDLE, FALSE,
                                parent_pid);
    if (!parent) {
        fprintf(stderr, "QForkChildMain: OpenProcess(%lu) gle=%lu\n",
                parent_pid, GetLastError());
        return 1;
    }

    if (!QForkChildAttach(parent, control_handle)) {
        CloseHandle(parent);
        return 1;
    }

    HANDLE local_payload = NULL;
    if (!DuplicateHandle(parent, (HANDLE)payload_handle, GetCurrentProcess(),
                         &local_payload, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        fprintf(stderr, "QForkChildMain: DuplicateHandle payload gle=%lu\n",
                GetLastError());
        CloseHandle(parent);
        return 1;
    }

    QForkPayloadHeader *hdr = (QForkPayloadHeader *)MapViewOfFile(
        local_payload, FILE_MAP_COPY, 0, 0, 0);
    if (!hdr) {
        fprintf(stderr, "QForkChildMain: MapViewOfFile payload gle=%lu\n",
                GetLastError());
        CloseHandle(local_payload);
        CloseHandle(parent);
        return 1;
    }

    if (hdr->magic != QFORK_MAGIC) {
        fprintf(stderr, "QForkChildMain: bad payload magic 0x%08x\n", hdr->magic);
        UnmapViewOfFile(hdr);
        CloseHandle(local_payload);
        CloseHandle(parent);
        return 1;
    }

    crc64_init();
    void *redisData = (char *)hdr + sizeof(QForkPayloadHeader);
    SetupRedisGlobals(redisData, hdr->redisDataSize, hdr->dictHashSeed,
                      (int)hdr->purpose);

    int rc = 1;
    if (hdr->purpose == CHILD_TYPE_RDB) {
        void *rsi = hdr->rsi_valid ? (void *)hdr->rsi : NULL;
        if (do_rdbSave(hdr->rdb_req, hdr->filename, rsi, hdr->rdb_flags) == 0)
            rc = 0;
    } else if (hdr->purpose == CHILD_TYPE_AOF) {
        if (do_aofRewrite(NULL) == 0)
            rc = 0;
    } else {
        fprintf(stderr, "QForkChildMain: unsupported purpose %u\n", hdr->purpose);
    }

    UnmapViewOfFile(hdr);
    CloseHandle(local_payload);
    CloseHandle(parent);
    return rc;
}

int main(int argc, char **argv) {
    InitTimeFunctions();
    InitThreadControl();
    FDAPI_Init();

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--QForkExit") == 0 && i + 1 < argc)
            return (int)strtol(argv[i + 1], NULL, 10);
    }

    int qf = argv_has_qfork(argc, argv);
    if (qf) {
        if (qf + 3 >= argc) {
            fprintf(stderr, "QFork: --QFork <control> <payload> <parent-pid>\n");
            return 1;
        }
        void *control = (void *)(uintptr_t)_strtoui64(argv[qf + 1], NULL, 10);
        void *payload = (void *)(uintptr_t)_strtoui64(argv[qf + 2], NULL, 10);
        unsigned long ppid = strtoul(argv[qf + 3], NULL, 10);
        return QForkChildMain(control, payload, ppid);
    }

    g_PersistenceDisabled = QForkPreparsePersistence(argc, argv);

    int skip_heap = is_check_tool(argv[0]) ||
                    checkForSentinelMode(argc, argv, argv[0]) ||
                    g_PersistenceDisabled;
    if (skip_heap) {
        g_BypassMemoryMapOnAlloc = 1;
        return redis_main(argc, argv);
    }

    if (!QForkParentInit(0))
        return 1;

    /*
     * jemalloc 5.3 + LG_PAGE=22 still OOMs ("16 bytes") during aeApiCreate
     * when those pages come from the mapped heap (narenas:1 did not help).
     * Keep VirtualAlloc so PING works. Real QFork COW needs bypass=0; until
     * that is fixed, win32RedisFork writes the RDB in the parent and reaps a
     * dummy --QForkExit child so checkChildrenDone still runs.
     */
    g_BypassMemoryMapOnAlloc = 1;

    int rc = redis_main(argc, argv);
    QForkShutdown();
    return rc;
}
