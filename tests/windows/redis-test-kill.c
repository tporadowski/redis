/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/* PATH shim named kill.exe. Tcl `exec kill` must signal a native redis-server
 * instead of Git's MSYS kill, which does not know those pids. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

static int parse_unix_signal(const wchar_t *text) {
    const wchar_t *s;
    if (text == NULL || text[0] != L'-')
        return -1;
    s = text + 1;
    if ((s[0] == L's' || s[0] == L'S') &&
        (s[1] == L'i' || s[1] == L'I') &&
        (s[2] == L'g' || s[2] == L'G'))
        s += 3;
    if (_wcsicmp(s, L"TERM") == 0) return 15;
    if (_wcsicmp(s, L"INT") == 0) return 2;
    if (_wcsicmp(s, L"KILL") == 0 || wcscmp(s, L"9") == 0) return 9;
    if (_wcsicmp(s, L"USR1") == 0) return 10;
    if (wcscmp(s, L"0") == 0) return 0;
    if (iswdigit(s[0])) return (int)wcstol(s, NULL, 10);
    return -1;
}

static int process_is_running(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    DWORD code = 0;
    if (process == NULL)
        return 0;
    if (!GetExitCodeProcess(process, &code))
        code = 0;
    CloseHandle(process);
    return code == STILL_ACTIVE;
}

static int terminate_pid(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (process == NULL)
        return 0;
    if (!TerminateProcess(process, 1)) {
        CloseHandle(process);
        return 0;
    }
    CloseHandle(process);
    return 1;
}

static int deliver_signal(DWORD pid, int sig) {
    wchar_t pipe_name[64];
    HANDLE pipe;
    DWORD wrote = 0;
    _snwprintf(pipe_name, sizeof(pipe_name) / sizeof(pipe_name[0]),
               L"\\\\.\\pipe\\redis-sig-%lu", pid);
    pipe = CreateFileW(pipe_name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (pipe == INVALID_HANDLE_VALUE)
        return 0;
    if (!WriteFile(pipe, &sig, sizeof(sig), &wrote, NULL) || wrote != sizeof(sig)) {
        CloseHandle(pipe);
        return 0;
    }
    CloseHandle(pipe);
    return 1;
}

int wmain(int argc, wchar_t **argv) {
    int i;
    int sig = 15;
    DWORD pid = 0;
    int saw_pid = 0;

    for (i = 1; i < argc; i++) {
        int parsed;
        if (argv[i][0] == L'-') {
            parsed = parse_unix_signal(argv[i]);
            if (parsed < 0) {
                fwprintf(stderr, L"kill: unknown signal %ls\n", argv[i]);
                return 1;
            }
            sig = parsed;
            continue;
        }
        pid = (DWORD)wcstoul(argv[i], NULL, 10);
        saw_pid = 1;
    }
    if (!saw_pid || pid == 0) {
        fwprintf(stderr, L"kill: missing pid\n");
        return 1;
    }
    if (sig == 0)
        return process_is_running(pid) ? 0 : 1;
    if ((sig == 2 || sig == 15) && deliver_signal(pid, sig))
        return 0;
    if (!process_is_running(pid)) {
        fwprintf(stderr, L"kill: %lu: No such process\n", pid);
        return 1;
    }
    if (!terminate_pid(pid)) {
        fwprintf(stderr, L"kill: %lu: Operation not permitted\n", pid);
        return 1;
    }
    return 0;
}
