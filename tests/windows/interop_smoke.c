/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/* Win32 interop smoke. pwin32 interop.cpp adapted to APIs that exist here. */
#include "Win32_Interop/Win32_Error.h"
#include "Win32_Interop/Win32_FDAPI.h"
#include "Win32_Interop/Win32_PThread.h"
#include "Win32_Interop/Win32_Signal.h"
#include "Win32_Interop/posix/unistd.h"
#include "Win32_Interop/posix/sys/resource.h"
#include "dict.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int failures;

static void check(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

struct secure_random_context {
    unsigned char bytes[64];
    int result;
};

static void *fill_secure_random(void *argument) {
    struct secure_random_context *context = argument;
    context->result = win32_secure_random_bytes(context->bytes,
                                                sizeof(context->bytes));
    return NULL;
}

static void test_secure_random(void) {
    struct secure_random_context contexts[4];
    pthread_t threads[4];
    int i, j;
    int seen_high_bit = 0;

    memset(contexts, 0, sizeof(contexts));
    memset(threads, 0, sizeof(threads));

    check(RAND_MAX == INT_MAX,
          "RAND_MAX must be 31-bit so skiplist/HNSW thresholds match POSIX");
    check(win32_secure_random_bytes(NULL, 0) == 0,
          "zero-length secure random requests should succeed");
    errno = 0;
    check(win32_secure_random_bytes(NULL, 1) == -1 && errno == EINVAL,
          "a NULL secure-random buffer should fail with EINVAL");

    for (i = 0; i < 4; i++) {
        check(pthread_create(&threads[i], NULL, fill_secure_random,
                             &contexts[i]) == 0,
              "concurrent secure random thread should start");
    }
    for (i = 0; i < 4; i++) {
        if (threads[i] != 0)
            check(pthread_join(threads[i], NULL) == 0,
                  "concurrent secure random thread should join");
        {
            int all_zero = 1;
            for (j = 0; j < (int)sizeof(contexts[i].bytes); j++) {
                if (contexts[i].bytes[j] != 0) all_zero = 0;
            }
            check(contexts[i].result == 0 && !all_zero,
                  "secure random output should succeed and contain entropy");
        }
        if (i != 0) {
            check(memcmp(contexts[0].bytes, contexts[i].bytes,
                         sizeof(contexts[0].bytes)) != 0,
                  "independent secure random outputs should differ");
        }
    }

    for (i = 0; i < 64; i++) {
        long value = random();
        check(value >= 0 && value <= RAND_MAX,
              "random() should return a nonnegative 31-bit value");
        if (value > 32767) seen_high_bit = 1;
        check(rand() >= 0 && rand() <= RAND_MAX,
              "rand() should share the 31-bit generator");
    }
    check(seen_high_bit,
          "random() must exceed the CRT 15-bit range");
}

static void test_llp64_widths(void) {
    check(DICTHT_SIZE(33) == (UINT64_C(1) << 33),
          "DICTHT_SIZE(33) must retain the bit above 32");
    check(dictNextExpForSize((UINT64_C(1) << 32) + 1) == 33,
          "dictNextExpForSize must return 33 for a size above 2^32");
    check(sizeof(unsigned long) == 4,
          "this smoke must run against the Win64 LLP64 ABI");
    check(sizeof(size_t) == 8 && sizeof(void *) == 8,
          "Win64 pointers and size_t must be 64-bit");
}

static void test_windows_path_comparison(void) {
    check(win32_utf8_strings_equal_ignore_case("Redis-PATH", "redis-path"),
          "Windows UTF-8 text should support Unicode ordinal case folding");
    check(win32_utf8_contains_ignore_case("C:\\Redis-SENTINEL.EXE",
                                          "redis-sentinel"),
          "Windows executable aliases should be found case-insensitively");
    check(win32_utf8_paths_equal(
              "C:/Redis/\xc3\x84of/Data.AOF",
              "c:\\redis\\\xc3\xa4of\\data.aof"),
          "Windows UTF-8 paths should compare with Unicode ordinal case folding");
    check(!win32_utf8_paths_equal("C:/Redis/data.aof",
                                  "C:/Redis/other.aof"),
          "different Windows UTF-8 paths should not compare equal");
}

static void test_error_translation(void) {
    sigset_t signals;
    sigset_t old_signals = 0;

    check(win32_errno_from_system_error(WSAEWOULDBLOCK) == EAGAIN,
          "WSAEWOULDBLOCK should map to POSIX EAGAIN");
    check(win32_errno_from_system_error(WSAEINPROGRESS) == EINPROGRESS,
          "WSAEINPROGRESS should map to POSIX EINPROGRESS");
    check(win32_errno_from_system_error(WSAECONNRESET) == ECONNRESET,
          "WSAECONNRESET should map to POSIX ECONNRESET");
    check(win32_errno_from_system_error(WSAEINTR) == ECANCELED,
          "WSAEINTR should map to POSIX ECANCELED");
    check(win32_errno_from_system_error(ERROR_OPERATION_ABORTED) == ECANCELED,
          "an aborted Windows operation should map to POSIX ECANCELED");
    check(win32_errno_from_system_error(0x7fffffff) == EIO,
          "unknown Windows errors should map to POSIX EIO");

    sigemptyset(&signals);
    check(sigaddset(&signals, SIGUSR2) == 0 &&
              sigismember(&signals, SIGUSR2) != 0,
          "Windows signal sets should represent SIGUSR2 safely");
    sigdelset(&signals, SIGUSR2);
    check(sigismember(&signals, SIGUSR2) == 0,
          "Windows signal sets should remove SIGUSR2 safely");

    errno = EBUSY;
    check(pthread_sigmask(SIG_BLOCK, &signals, &old_signals) == 0 &&
              errno == EBUSY,
          "the Windows pthread signal-mask no-op should preserve errno");
    errno = EBUSY;
    check(pthread_sigmask(999, &signals, &old_signals) == EINVAL &&
              errno == EBUSY,
          "pthread_sigmask should reject an invalid how without touching errno");
}

static void push_dir(char (*dirs)[MAX_PATH * 2], int *ndirs, const char *path) {
    if (*ndirs >= 32) return;
    snprintf(dirs[*ndirs], sizeof(dirs[0]), "%s", path);
    (*ndirs)++;
}

static void test_utf8_filesystem(void) {
    const char unicode_component[] =
        "redis-interop-\xe8\xb7\xaf\xe5\xbe\x84-\xf0\x9f\x98\x80";
    char suffix[64];
    char root[MAX_PATH];
    char filename[MAX_PATH];
    char hardlink[MAX_PATH];
    char renamed[MAX_PATH];
    char nested_dir[MAX_PATH];
    char nested_file[MAX_PATH];
    char dirs[32][MAX_PATH * 2];
    int ndirs = 0;
    FILE *file = NULL;
    char *full_filename = NULL;
    char *full_nested_file = NULL;
    char *full_root = NULL;
    char *saved_cwd = NULL;
    char *changed_cwd = NULL;
    char **matches = NULL;
    size_t match_count = 0;
    int found_name = 0;
    int found_glob = 0;
    struct stat statbuf;
    int i;

    snprintf(suffix, sizeof(suffix), "-%lu-%lu",
             (unsigned long)GetCurrentProcessId(),
             (unsigned long)GetTickCount());
    snprintf(root, sizeof(root), "%s%s", unicode_component, suffix);
    snprintf(filename, sizeof(filename), "%s/\xe6\x95\xb0\xe6\x8d\xae.txt",
             root);
    snprintf(hardlink, sizeof(hardlink), "%s/\xe9\x93\xbe\xe6\x8e\xa5.txt",
             root);
    snprintf(renamed, sizeof(renamed),
             "%s/\xe9\x87\x8d\xe5\x91\xbd\xe5\x90\x8d.txt", root);
    snprintf(nested_dir, sizeof(nested_dir), "%s/group-a", root);
    snprintf(nested_file, sizeof(nested_file),
             "%s/\xe9\x85\x8d\xe7\xbd\xae-x.conf", nested_dir);

    check(replace_mkdir(root) == 0, "UTF-8 directory creation should succeed");
    if (replace_stat(root, &statbuf) != 0) goto cleanup;
    push_dir(dirs, &ndirs, root);

    file = replace_fopen(filename, "wb");
    check(file != NULL, "UTF-8 file creation should succeed");
    if (file != NULL) {
        const char payload[] = "unicode-path-payload";
        check(fwrite(payload, 1, sizeof(payload) - 1, file) ==
                  sizeof(payload) - 1,
              "UTF-8 file should accept data");
        check(fclose(file) == 0, "UTF-8 file should close cleanly");
        file = NULL;
    }
    check(replace_stat(filename, &statbuf) == 0 && statbuf.st_size == 20,
          "UTF-8 file should be visible through wide stat");

    file = replace_fopen(filename, "r");
    check(file != NULL, "UTF-8 file should reopen through wide fopen");
    if (file != NULL) {
        file = replace_freopen(filename, "rb", file);
        check(file != NULL, "UTF-8 file should reopen through wide freopen");
        if (file != NULL) {
            check(fclose(file) == 0, "wide freopen stream should close cleanly");
            file = NULL;
        }
    }

    full_filename = win32_get_full_path_utf8(filename);
    check(full_filename != NULL &&
              strstr(full_filename, unicode_component) != NULL,
          "GetFullPathNameW result should round-trip as UTF-8");

    {
        char reserved_path[MAX_PATH];
        wchar_t *wide_reserved;
        char *before_reserved_cwd;
        char *after_reserved_cwd;

        snprintf(reserved_path, sizeof(reserved_path), "%s/NUL.txt", root);
        wide_reserved = win32_utf8_path_to_wide(reserved_path);
        check(wide_reserved != NULL && wcsncmp(wide_reserved, L"\\\\?\\", 4) == 0,
              "reserved DOS path components should use verbatim wide paths");
        free(wide_reserved);

        before_reserved_cwd = win32_get_current_directory_utf8();
        errno = 0;
        check(win32_set_current_directory_utf8(reserved_path) == -1 &&
                  errno == ENAMETOOLONG,
              "working directories should reject verbatim-only components");
        after_reserved_cwd = win32_get_current_directory_utf8();
        check(before_reserved_cwd != NULL && after_reserved_cwd != NULL &&
                  strcmp(before_reserved_cwd, after_reserved_cwd) == 0,
              "rejected verbatim working directory should leave CWD unchanged");
        free(before_reserved_cwd);
        free(after_reserved_cwd);
    }

    {
        win32_utf8_dir *dir = win32_opendir_utf8(root);
        check(dir != NULL, "UTF-8 directory enumeration should open");
        if (dir != NULL) {
            const char *name;
            while ((name = win32_readdir_utf8(dir)) != NULL) {
                if (strcmp(name, "\xe6\x95\xb0\xe6\x8d\xae.txt") == 0)
                    found_name = 1;
            }
            check(win32_closedir_utf8(dir) == 0,
                  "UTF-8 directory enumeration should close");
        }
    }
    check(found_name, "wide directory enumeration should preserve UTF-8 names");

    check(link(filename, hardlink) == 0, "UTF-8 hard-link creation should succeed");
    {
        wchar_t *wide_src = win32_utf8_path_to_wide(hardlink);
        wchar_t *wide_dst = win32_utf8_path_to_wide(renamed);
        BOOL moved = (wide_src && wide_dst) &&
            MoveFileExW(wide_src, wide_dst,
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        check(moved, "UTF-8 rename should succeed");
        free(wide_src);
        free(wide_dst);
    }
    check(replace_stat(renamed, &statbuf) == 0,
          "renamed UTF-8 hard link should exist");

    {
        char pattern[MAX_PATH];
        snprintf(pattern, sizeof(pattern), "%s/*", root);
        check(win32_glob_utf8(pattern, &matches, &match_count) == 0,
              "wide glob should succeed on a UTF-8 directory");
        for (i = 0; i < (int)match_count; i++) {
            if (full_filename != NULL && strcmp(matches[i], full_filename) == 0)
                found_glob = 1;
        }
        check(found_glob, "wide glob should return the UTF-8 filename");
        win32_globfree_utf8(matches, match_count);
        matches = NULL;
        match_count = 0;
    }

    check(replace_mkdir(nested_dir) == 0,
          "nested UTF-8 glob directory creation should succeed");
    if (replace_stat(nested_dir, &statbuf) == 0)
        push_dir(dirs, &ndirs, nested_dir);
    file = replace_fopen(nested_file, "wb");
    check(file != NULL, "nested UTF-8 glob file creation should succeed");
    if (file != NULL) {
        check(fclose(file) == 0, "nested UTF-8 glob file should close cleanly");
        file = NULL;
    }
    full_nested_file = win32_get_full_path_utf8(nested_file);
    {
        char pattern[MAX_PATH];
        int found_nested = 0;
        snprintf(pattern, sizeof(pattern),
                 "%s/group-[ab]/\xe9\x85\x8d\xe7\xbd\xae-?.conf", root);
        check(win32_glob_utf8(pattern, &matches, &match_count) == 0,
              "wide glob should expand wildcard directory components");
        for (i = 0; i < (int)match_count; i++) {
            if (full_nested_file != NULL &&
                strcmp(matches[i], full_nested_file) == 0)
                found_nested = 1;
        }
        check(match_count == 1 && found_nested,
              "wide glob should implement bracket and question-mark matching");
        win32_globfree_utf8(matches, match_count);
        matches = NULL;
        match_count = 0;
    }

    {
        char deep[MAX_PATH * 2];
        snprintf(deep, sizeof(deep), "%s", root);
        for (i = 0; i < 12; i++) {
            char component[48];
            snprintf(component, sizeof(component),
                     "/segment-%02d-abcdefghijklmnop", i);
            strncat(deep, component, sizeof(deep) - strlen(deep) - 1);
            check(replace_mkdir(deep) == 0,
                  "extended-length directory creation should succeed");
            if (replace_stat(deep, &statbuf) != 0) break;
            push_dir(dirs, &ndirs, deep);
        }
        check(strlen(deep) >= MAX_PATH,
              "interop test should cross the legacy MAX_PATH boundary");
        {
            char deep_file[MAX_PATH * 2];
            snprintf(deep_file, sizeof(deep_file),
                     "%s/\xe6\xb7\xb1\xe5\xb1\x82.txt", deep);
            file = replace_fopen(deep_file, "wb");
            check(file != NULL, "extended-length UTF-8 file creation should succeed");
            if (file != NULL) {
                check(fclose(file) == 0,
                      "extended-length UTF-8 file should close cleanly");
                file = NULL;
                check(replace_stat(deep_file, &statbuf) == 0,
                      "extended-length UTF-8 file should be stat-able");
            }
            check(replace_unlink(deep_file) == 0,
                  "extended-length UTF-8 file should be removable");
        }

        {
            char *before_long_cwd = win32_get_current_directory_utf8();
            errno = 0;
            check(before_long_cwd != NULL &&
                      win32_set_current_directory_utf8(deep) == -1 &&
                      errno == ENAMETOOLONG,
                  "process working directory should reject paths at MAX_PATH");
            {
                char *after_long_cwd = win32_get_current_directory_utf8();
                check(before_long_cwd != NULL && after_long_cwd != NULL &&
                          strcmp(before_long_cwd, after_long_cwd) == 0,
                      "rejected long working directory should leave CWD unchanged");
                free(after_long_cwd);
            }
            free(before_long_cwd);
        }
    }

    {
        char missing[MAX_PATH];
        int fd;
        snprintf(missing, sizeof(missing),
                 "%s/missing-\xe6\x96\x87\xe4\xbb\xb6", root);
        errno = 0;
        fd = open(missing, O_RDONLY, 0);
        check(fd == -1 && errno == ENOENT,
              "wide FD open should preserve ENOENT");
        if (fd != -1) close(fd);
    }

    {
        const char invalid_utf8[] = {'b', 'a', 'd', '-', (char)0xff, '\0'};
        wchar_t *wide;
        errno = 0;
        wide = win32_utf8_to_wide(invalid_utf8);
        check(wide == NULL && errno == EILSEQ,
              "invalid UTF-8 should be rejected at the Windows boundary");
        free(wide);
    }

    {
        const wchar_t env_name[] = L"REDIS_INTEROP_UTF8";
        const wchar_t env_value[] = L"\x8def\x5f84-\xd83d\xde00";
        char *value;
        char *cached;

        check(SetEnvironmentVariableW(env_name, env_value) != 0,
              "Unicode environment test value should be set");
        value = win32_getenv_utf8("REDIS_INTEROP_UTF8");
        check(value != NULL &&
                  strcmp(value,
                         "\xe8\xb7\xaf\xe5\xbe\x84-\xf0\x9f\x98\x80") == 0,
              "wide getenv should return strict UTF-8");
        free(value);

        cached = win32_getenv_utf8_cached("REDIS_INTEROP_UTF8");
        check(cached != NULL &&
                  strcmp(cached,
                         "\xe8\xb7\xaf\xe5\xbe\x84-\xf0\x9f\x98\x80") == 0,
              "cached wide getenv should return UTF-8");
        check(SetEnvironmentVariableW(env_name, L"updated") != 0,
              "Unicode environment test value should update");
        cached = win32_getenv_utf8_cached("REDIS_INTEROP_UTF8");
        check(cached != NULL && strcmp(cached, "updated") == 0,
              "cached wide getenv should refresh without leaking lookups");
        cached = win32_getenv_utf8_cached("redis_interop_utf8");
        check(cached != NULL && strcmp(cached, "updated") == 0,
              "cached wide getenv should follow Windows case-insensitive names");
        SetEnvironmentVariableW(env_name, NULL);
        check(win32_getenv_utf8_cached("REDIS_INTEROP_UTF8") == NULL,
              "cached wide getenv should observe variable removal");

        errno = 0;
        SetLastError(ERROR_SUCCESS);
        check(win32_getenv_utf8_cached(NULL) == NULL && errno == EINVAL &&
                  GetLastError() == ERROR_INVALID_PARAMETER,
              "cached wide getenv should reject a null variable name");
    }

    saved_cwd = win32_get_current_directory_utf8();
    full_root = win32_get_full_path_utf8(root);
    check(saved_cwd != NULL && full_root != NULL,
          "wide current-directory helpers should return UTF-8 paths");
    if (saved_cwd != NULL && full_root != NULL) {
        check(win32_set_current_directory_utf8(root) == 0,
              "wide current-directory setter should accept UTF-8");
        changed_cwd = win32_get_current_directory_utf8();
        check(changed_cwd != NULL && strcmp(changed_cwd, full_root) == 0,
              "wide current-directory getter should preserve UTF-8");
        check(win32_set_current_directory_utf8(saved_cwd) == 0,
              "wide current-directory setter should restore the directory");
    }


cleanup:
    if (file != NULL) fclose(file);
    free(changed_cwd);
    free(full_root);
    free(saved_cwd);
    free(full_filename);
    free(full_nested_file);
    win32_globfree_utf8(matches, match_count);
    if (replace_stat(renamed, &statbuf) == 0)
        check(replace_remove(renamed) == 0,
              "wide remove should delete a UTF-8 path");
    replace_unlink(hardlink);
    replace_unlink(nested_file);
    replace_unlink(filename);
    for (i = ndirs - 1; i >= 0; i--) {
        check(replace_rmdir(dirs[i]) == 0,
              "interop test directory cleanup should succeed");
    }
}

static void *return_thread_argument(void *argument) {
    return argument;
}

struct pthread_hold_context {
    HANDLE started;
    HANDLE release;
};

static void *hold_until_released(void *argument) {
    struct pthread_hold_context *context = argument;
    SetEvent(context->started);
    WaitForSingleObject(context->release, INFINITE);
    return argument;
}

static void test_pthread_join_result(void) {
    int marker = 0x5a17;
    pthread_attr_t attributes;
    size_t stack_size = 0;
    pthread_t thread = 0;
    pthread_t detached = 0;
    void *result = NULL;
    struct pthread_hold_context hold;

    check(pthread_attr_init(&attributes) == 0 &&
              pthread_attr_getstacksize(&attributes, &stack_size) == 0 &&
              stack_size == 0,
          "pthread attributes should initialize with the default stack size");
    check(pthread_attr_setstacksize(&attributes, 8 * 1024 * 1024) == 0 &&
              pthread_attr_getstacksize(&attributes, &stack_size) == 0 &&
              stack_size == 8 * 1024 * 1024,
          "pthread stack-size attributes should round trip on Win64");
    check(pthread_create(&thread, &attributes, return_thread_argument,
                         &marker) == 0,
          "pthread_create should retain a joinable Windows handle");
    if (thread == 0) return;
    check(pthread_join(thread, &result) == 0 && result == &marker,
          "pthread_join should return the thread result pointer");
    check(pthread_join(thread, NULL) == ESRCH,
          "a second join should report that the thread is gone");
    check(pthread_join(pthread_self(), NULL) == EDEADLK,
          "joining the calling thread should be EDEADLK");
    check(pthread_join((pthread_t)0x2, NULL) == ESRCH,
          "joining an unknown pthread_t should be ESRCH");

    memset(&hold, 0, sizeof(hold));
    hold.started = CreateEventW(NULL, TRUE, FALSE, NULL);
    hold.release = CreateEventW(NULL, TRUE, FALSE, NULL);
    check(hold.started != NULL && hold.release != NULL,
          "detach test events should be created");
    if (hold.started == NULL || hold.release == NULL) goto detach_cleanup;
    check(pthread_create(&detached, NULL, hold_until_released, &hold) == 0,
          "detach test thread should be created");
    if (detached == 0) goto detach_cleanup;
    check(WaitForSingleObject(hold.started, 5000) == WAIT_OBJECT_0,
          "detach test thread should start");
    check(pthread_detach(detached) == 0,
          "pthread_detach should mark a live thread detached");
    check(pthread_join(detached, NULL) == EINVAL,
          "joining a detached thread should be EINVAL");
    check(pthread_detach(detached) == EINVAL,
          "a second detach of a live thread should be EINVAL");
    SetEvent(hold.release);

detach_cleanup:
    if (hold.release != NULL) {
        SetEvent(hold.release);
        CloseHandle(hold.release);
    }
    if (hold.started != NULL) CloseHandle(hold.started);
}

struct pthread_identity_context {
    HANDLE start_event;
    volatile LONG completed;
    pthread_t observed_self;
};

static void *capture_pthread_identity(void *argument) {
    struct pthread_identity_context *context = argument;

    WaitForSingleObject(context->start_event, INFINITE);
    context->observed_self = pthread_self();
    InterlockedExchange(&context->completed, 1);
    return argument;
}

static int wait_for_thread_completion(struct pthread_identity_context *context) {
    int attempts;
    for (attempts = 0; attempts < 500; attempts++) {
        if (InterlockedCompareExchange(&context->completed, 0, 0) != 0)
            return 1;
        Sleep(10);
    }
    return 0;
}

static void test_pthread_identity(void) {
    struct pthread_identity_context first;
    struct pthread_identity_context second;
    pthread_t first_thread = 0;
    pthread_t second_thread = 0;

    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    first.start_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    second.start_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    check(first.start_event != NULL && second.start_event != NULL,
          "pthread identity test events should be created");
    if (first.start_event == NULL || second.start_event == NULL) goto cleanup;

    check(pthread_create(&first_thread, NULL, capture_pthread_identity,
                         &first) == 0,
          "first pthread identity thread should be created");
    check(pthread_create(&second_thread, NULL, capture_pthread_identity,
                         &second) == 0,
          "second pthread identity thread should be created");
    if (first_thread == 0 || second_thread == 0) goto cleanup;

    check(first_thread != second_thread,
          "concurrent pthread identities should remain distinct");
    SetEvent(first.start_event);
    SetEvent(second.start_event);
    check(wait_for_thread_completion(&first),
          "first pthread identity thread should complete");
    check(wait_for_thread_completion(&second),
          "second pthread identity thread should complete");
    check(first.observed_self == first_thread,
          "pthread_self should match the first opaque pthread identity");
    check(second.observed_self == second_thread,
          "pthread_self should match the second opaque pthread identity");
    check((pthread_self() & 1) != 0,
          "the main thread identity should stay outside the record-pointer namespace");
    check(pthread_self() != first_thread && pthread_self() != second_thread,
          "worker pthread identities should differ from the caller");

cleanup:
    if (second_thread != 0)
        check(pthread_join(second_thread, NULL) == 0,
              "second pthread identity thread should join");
    if (first_thread != 0)
        check(pthread_join(first_thread, NULL) == 0,
              "first pthread identity thread should join");
    if (second.start_event != NULL) CloseHandle(second.start_event);
    if (first.start_event != NULL) CloseHandle(first.start_event);
}

static void test_proc_address_policy(void) {
    typedef DWORD (WINAPI *GetCurrentProcessIdFunction)(void);
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    GetCurrentProcessIdFunction function = NULL;
    const char non_ascii_name[] = {'G', 'e', 't', (char)0xc3, '\0'};

    check(kernel32 != NULL, "kernel32 should already be loaded");
    if (kernel32 == NULL) return;

    errno = EBUSY;
    check(win32_get_proc_address(kernel32, "GetCurrentProcessId", &function,
                                 sizeof(function)) == 0 &&
              function != NULL && function() == GetCurrentProcessId(),
          "ASCII PE export names should resolve to typed function pointers");
    check(errno == EBUSY,
          "procedure lookup should preserve the caller's errno");

    function = NULL;
    SetLastError(ERROR_SUCCESS);
    check(win32_get_proc_address(kernel32, non_ascii_name, &function,
                                 sizeof(function)) == -1 &&
              function == NULL && GetLastError() == ERROR_INVALID_NAME,
          "non-ASCII PE export names should be rejected explicitly");

    SetLastError(ERROR_SUCCESS);
    check(win32_get_proc_address(kernel32, "GetCurrentProcessId", &function,
                                 sizeof(function) - 1) == -1 &&
              GetLastError() == ERROR_INVALID_PARAMETER,
          "procedure lookup should reject incompatible pointer sizes");
}

static void test_dns_ascii_policy(void) {
    struct addrinfo hints;
    struct addrinfo *result;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    result = (struct addrinfo *)(uintptr_t)1;
    check(getaddrinfo("host-\xc3\xa9", "6379", &hints, &result) ==
              EAI_NONAME && result == NULL,
          "non-ASCII DNS nodes should be rejected before Winsock");
    result = (struct addrinfo *)(uintptr_t)1;
    check(getaddrinfo("localhost", "port-\xc3\xa9", &hints, &result) ==
              EAI_SERVICE && result == NULL,
          "non-ASCII DNS services should be rejected before Winsock");
}

static void test_address_conversion(void) {
    struct in_addr ipv4;
    struct in_addr ipv4_roundtrip;
    struct in6_addr ipv6;
    struct in6_addr ipv6_roundtrip;
    char text[INET6_ADDRSTRLEN];

    memset(&ipv4, 0, sizeof(ipv4));
    memset(&ipv4_roundtrip, 0, sizeof(ipv4_roundtrip));
    memset(&ipv6, 0, sizeof(ipv6));
    memset(&ipv6_roundtrip, 0, sizeof(ipv6_roundtrip));
    memset(text, 0, sizeof(text));

    check(inet_pton(AF_INET, "192.0.2.1", &ipv4) == 1,
          "inet_pton should parse IPv4");
    check(inet_ntop(AF_INET, &ipv4, text, sizeof(text)) == text,
          "inet_ntop should format IPv4");
    check(strcmp(text, "192.0.2.1") == 0,
          "inet_ntop should produce the expected IPv4 text");
    check(inet_pton(AF_INET, text, &ipv4_roundtrip) == 1 &&
              memcmp(&ipv4, &ipv4_roundtrip, sizeof(ipv4)) == 0,
          "IPv4 text should round trip");

    memset(text, 0, sizeof(text));
    check(inet_pton(AF_INET6, "2001:db8::1", &ipv6) == 1,
          "inet_pton should parse IPv6");
    check(inet_ntop(AF_INET6, &ipv6, text, sizeof(text)) == text,
          "inet_ntop should format IPv6");
    check(strchr(text, '[') == NULL && strchr(text, ']') == NULL,
          "inet_ntop should not add IPv6 brackets");
    check(inet_pton(AF_INET6, text, &ipv6_roundtrip) == 1 &&
              memcmp(&ipv6, &ipv6_roundtrip, sizeof(ipv6)) == 0,
          "IPv6 text should round trip");

    check(inet_pton(AF_INET6, "not-an-address", &ipv6) == 0,
          "inet_pton should reject invalid IPv6 text");

    {
        char small[4];
        memset(small, 0, sizeof(small));
        errno = 0;
        check(inet_ntop(AF_INET6, &ipv6_roundtrip, small, sizeof(small)) == NULL,
              "inet_ntop should fail for a short output buffer");
    }

    errno = 0;
    check(inet_ntop(AF_UNSPEC, &ipv4, text, sizeof(text)) == NULL,
          "inet_ntop should reject unsupported families");
    errno = 0;
    check(inet_pton(AF_UNSPEC, "192.0.2.1", &ipv4) == -1,
          "inet_pton should reject unsupported families");
}

static long long rusage_total_us(const struct rusage *ru) {
    return (long long)ru->ru_utime.tv_sec * 1000000 + ru->ru_utime.tv_usec +
           (long long)ru->ru_stime.tv_sec * 1000000 + ru->ru_stime.tv_usec;
}

static int rusage_timeval_ok(const struct timeval *tv) {
    return tv->tv_sec >= 0 && tv->tv_usec >= 0 && tv->tv_usec < 1000000;
}

static void burn_cpu(void) {
    volatile unsigned x = 1;
    int i;
    for (i = 0; i < 20000000; i++)
        x = x * 1664525u + 1013904223u;
}

static void test_getrusage(void) {
    struct rusage self_usage;
    struct rusage thread_usage;
    struct rusage children;
    int spins;

    errno = 0;
    check(getrusage(RUSAGE_SELF, NULL) == -1 && errno == EFAULT,
          "getrusage should reject a NULL usage block with EFAULT");
    errno = 0;
    check(getrusage(99, &self_usage) == -1 && errno == EINVAL,
          "getrusage should reject an unknown who with EINVAL");

    memset(&children, 0xff, sizeof(children));
    check(getrusage(RUSAGE_CHILDREN, &children) == 0,
          "getrusage RUSAGE_CHILDREN should succeed");
    check(rusage_total_us(&children) == 0,
          "Windows does not accumulate child CPU into RUSAGE_CHILDREN");

    memset(&self_usage, 0xff, sizeof(self_usage));
    check(getrusage(RUSAGE_SELF, &self_usage) == 0,
          "getrusage RUSAGE_SELF should succeed");
    check(rusage_timeval_ok(&self_usage.ru_utime) &&
              rusage_timeval_ok(&self_usage.ru_stime),
          "RUSAGE_SELF times should be normalized timevals");

    for (spins = 0; spins < 8 && rusage_total_us(&self_usage) == 0; spins++) {
        burn_cpu();
        check(getrusage(RUSAGE_SELF, &self_usage) == 0,
              "getrusage RUSAGE_SELF should succeed after work");
    }
    check(rusage_total_us(&self_usage) > 0,
          "GetProcessTimes should report nonzero CPU after work");

    check(getrusage(RUSAGE_THREAD, &thread_usage) == 0,
          "getrusage RUSAGE_THREAD should succeed");
    check(rusage_timeval_ok(&thread_usage.ru_utime) &&
              rusage_timeval_ok(&thread_usage.ru_stime),
          "RUSAGE_THREAD times should be normalized timevals");
    check(rusage_total_us(&thread_usage) > 0,
          "GetThreadTimes should report nonzero CPU after work");
    check(rusage_total_us(&thread_usage) <= rusage_total_us(&self_usage),
          "thread CPU should not exceed process CPU");
}

static void close_pipe(int pipefds[2]) {
    if (pipefds[0] != -1) {
        close(pipefds[0]);
        pipefds[0] = -1;
    }
    if (pipefds[1] != -1) {
        close(pipefds[1]);
        pipefds[1] = -1;
    }
}

static void test_synthetic_fd_ftruncate(void) {
    int fd = -1;
    int stale_fd = -1;
    char filename[] = "redis-interop-ftruncate-XXXXXX";
    const char payload[] = "0123456789abcdef";
    char byte = 0;
    struct stat statbuf;

    memset(&statbuf, 0, sizeof(statbuf));
    fd = mkstemp(filename);
    check(fd != -1, "ftruncate test temporary file creation should succeed");
    if (fd == -1) return;

    check(write(fd, payload, sizeof(payload) - 1) ==
              (ssize_t)(sizeof(payload) - 1),
          "synthetic file descriptor should accept the initial payload");
    check(replace_stat(filename, &statbuf) == 0 &&
              statbuf.st_size == (off_t)(sizeof(payload) - 1),
          "synthetic file descriptor should report the initial file size");
    check(lseek(fd, 7, SEEK_SET) == 7,
          "ftruncate test should position the synthetic file descriptor");
    check(ftruncate(fd, 5) == 0,
          "ftruncate should shrink a file through the synthetic descriptor map");
    check(replace_stat(filename, &statbuf) == 0 && statbuf.st_size == 5,
          "ftruncate should persist the shortened synthetic file size");
    check(lseek(fd, 0, SEEK_CUR) == 7,
          "ftruncate shrink should preserve the current file offset");

    check(ftruncate(fd, 32) == 0,
          "ftruncate should extend a file through the synthetic descriptor map");
    check(replace_stat(filename, &statbuf) == 0 && statbuf.st_size == 32,
          "ftruncate should persist the extended synthetic file size");
    check(lseek(fd, 0, SEEK_CUR) == 7,
          "ftruncate extension should preserve the current file offset");

    errno = 0;
    check(lseek(fd, -1, SEEK_SET) == -1 && errno == EINVAL,
          "signed lseek should preserve negative-offset errors");

    errno = 0;
    check(ftruncate(fd, -1) == -1 && errno == EINVAL,
          "ftruncate should reject a negative file size");

    stale_fd = fd;
    check(close(fd) == 0, "ftruncate test temporary file should close cleanly");
    fd = -1;
    check(replace_unlink(filename) == 0,
          "ftruncate test temporary file should be removed");

    errno = 0;
    check(write(stale_fd, &byte, 1) == -1 && errno == EBADF,
          "write should reject a stale synthetic file descriptor");
    errno = 0;
    check(read(stale_fd, &byte, 1) == -1 && errno == EBADF,
          "read should reject a stale synthetic file descriptor");
    errno = 0;
    check(fsync(stale_fd) == -1 && errno == EBADF,
          "fsync should reject a stale synthetic file descriptor");
    errno = 0;
    check(ftruncate(stale_fd, 0) == -1 && errno == EBADF,
          "ftruncate should reject a stale synthetic file descriptor");
}

static void test_eventloop_pipe(void) {
    int pipefds[2] = {-1, -1};
    unsigned char byte = 0;
    unsigned char payload[4096];
    unsigned char received[4096];
    size_t written = 0;
    size_t received_len = 0;
    size_t i;
    int result;
    int flags;
    int read_fd;
    int write_fd;
    fd_set readfds;
    struct timeval timeout;

    check(pipe(pipefds) == 0, "event-loop pipe creation should succeed");
    if (pipefds[0] == -1 || pipefds[1] == -1) return;

    flags = fcntl(pipefds[0], F_GETFL, 0);
    check(flags != -1, "event-loop pipe read flags should be available");
    result = fcntl(pipefds[0], F_SETFL, flags | O_NONBLOCK);
    check(result == 0, "event-loop pipe read endpoint should become nonblocking");

    errno = 0;
    check(read(pipefds[0], &byte, 1) == -1 &&
              (errno == EAGAIN || errno == EWOULDBLOCK),
          "empty nonblocking event-loop pipe should report EAGAIN");

    for (i = 0; i < sizeof(payload); i++)
        payload[i] = (unsigned char)((i * 37) & 0xff);

    while (written < sizeof(payload)) {
        ssize_t nwritten = write(pipefds[1], payload + written,
                                 sizeof(payload) - written);
        if (nwritten <= 0) break;
        written += (size_t)nwritten;
    }
    check(written == sizeof(payload),
          "event-loop pipe should accept the complete byte stream");

    FD_ZERO(&readfds);
    FD_SET(pipefds[0], &readfds);
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    result = select(pipefds[0] + 1, &readfds, NULL, NULL, &timeout);
    check(result == 1 && FD_ISSET(pipefds[0], &readfds),
          "select should restore ready synthetic descriptors to the result set");

    memset(received, 0, sizeof(received));
    while (received_len < written) {
        ssize_t chunk = read(pipefds[0], received + received_len,
                             written - received_len);
        if (chunk <= 0) break;
        received_len += (size_t)chunk;
    }
    check(received_len == sizeof(payload) &&
              memcmp(payload, received, sizeof(payload)) == 0,
          "event-loop pipe should preserve byte-stream contents");

    read_fd = pipefds[0];
    write_fd = pipefds[1];
    check(close(write_fd) == 0, "event-loop pipe writer should close cleanly");
    pipefds[1] = -1;

    check(read(read_fd, &byte, 1) == 0,
          "event-loop pipe reader should observe EOF after writer close");

    check(close(read_fd) == 0, "event-loop pipe reader should close cleanly");
    pipefds[0] = -1;

    close_pipe(pipefds);
}

static void test_iocp_blocking_transition(void) {
    int pipefds[2] = {-1, -1};
    HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    int result;
    int flags;
    struct timeval timeout;
    unsigned char byte = 0;
    DWORD started;
    ssize_t nread;
    DWORD elapsed;

    memset(&timeout, 0, sizeof(timeout));
    timeout.tv_usec = 100000;
    check(iocp != NULL, "IOCP creation should succeed");
    check(pipe(pipefds) == 0, "IOCP blocking test pipe creation should succeed");
    if (iocp == NULL || pipefds[0] == -1 || pipefds[1] == -1) goto cleanup;

    result = FDAPI_SocketAttachIOCP(pipefds[0], iocp);
    check(result != 0, "event-loop pipe reader should attach to IOCP");
    if (result == 0) goto cleanup;

    flags = fcntl(pipefds[0], F_GETFL, 0);
    check(flags != -1, "IOCP-backed reader flags should be available");
    result = fcntl(pipefds[0], F_SETFL, flags & ~O_NONBLOCK);
    check(result == 0, "IOCP-backed reader should switch to blocking mode");
    if (result != 0) goto cleanup;

    result = setsockopt(pipefds[0], SOL_SOCKET, SO_RCVTIMEO,
                        &timeout, sizeof(timeout));
    check(result == 0, "IOCP-backed reader should accept a receive timeout");
    if (result != 0) goto cleanup;

    started = GetTickCount();
    errno = 0;
    nread = read(pipefds[0], &byte, 1);
    elapsed = GetTickCount() - started;
    check(nread == -1 && (errno == ETIMEDOUT || errno == 138 ||
                          errno == EAGAIN || errno == EWOULDBLOCK),
          "blocking IOCP-backed read should report a timeout");
    check(elapsed >= 50,
          "blocking IOCP-backed read should wait instead of returning immediately");

cleanup:
    close_pipe(pipefds);
    if (iocp != NULL) CloseHandle(iocp);
}

static void test_socket_duplication(void) {
    int data_pipe[2] = {-1, -1};
    int exit_pipe[2] = {-1, -1};
    int child_data_writer = -1;
    int child_exit_reader = -1;
    unsigned char byte = 0;
    WSAPROTOCOL_INFO data_protocol;
    WSAPROTOCOL_INFO exit_protocol;

    memset(&data_protocol, 0, sizeof(data_protocol));
    memset(&exit_protocol, 0, sizeof(exit_protocol));

    check(pipe(data_pipe) == 0 && pipe(exit_pipe) == 0,
          "two event-loop pipes should be available for QFork duplication");
    if (data_pipe[0] == -1 || data_pipe[1] == -1 ||
        exit_pipe[0] == -1 || exit_pipe[1] == -1) {
        close_pipe(data_pipe);
        close_pipe(exit_pipe);
        return;
    }

    check(FDAPI_WSADuplicateSocket(data_pipe[1], GetCurrentProcessId(),
                                   &data_protocol) == 0,
          "QFork data writer duplication should succeed");
    check(FDAPI_WSADuplicateSocket(exit_pipe[0], GetCurrentProcessId(),
                                   &exit_protocol) == 0,
          "QFork exit reader duplication should succeed after the data writer");

    child_data_writer = FDAPI_WSASocketFromInfo(&data_protocol);
    child_exit_reader = FDAPI_WSASocketFromInfo(&exit_protocol);
    check(child_data_writer != -1 && child_exit_reader != -1,
          "QFork child sockets should be recreated from protocol info");
    if (child_data_writer == -1 || child_exit_reader == -1) goto cleanup;

    /* Write before closing the parent copy. FDAPI_close shutdown(SD_SEND)s
     * the shared TCP endpoint, so a same-process duplicate cannot send
     * after the parent writer is closed. */
    byte = 0x5a;
    check(write(child_data_writer, &byte, 1) == 1,
          "duplicated data writer should send bytes");
    byte = 0;
    check(read(data_pipe[0], &byte, 1) == 1 && byte == 0x5a,
          "parent data reader should receive duplicated-writer bytes");

    check(close(data_pipe[1]) == 0,
          "parent data writer should close after duplication");
    data_pipe[1] = -1;
    check(close(exit_pipe[0]) == 0,
          "parent exit reader should close after duplication");
    exit_pipe[0] = -1;

cleanup:
    if (child_data_writer != -1) close(child_data_writer);
    if (child_exit_reader != -1) close(child_exit_reader);
    close_pipe(data_pipe);
    close_pipe(exit_pipe);
}

static void ran(const char *name) {
    printf("  %s\n", name);
    fflush(stdout);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    FDAPI_Init();

    test_windows_path_comparison();
    ran("path");
    test_error_translation();
    ran("errno");
    test_utf8_filesystem();
    ran("utf8-fs");
    test_pthread_join_result();
    ran("pthread-join");
    test_pthread_identity();
    ran("pthread-id");
    test_secure_random();
    ran("random");
    test_llp64_widths();
    ran("llp64");
    test_proc_address_policy();
    ran("getproc");
    test_dns_ascii_policy();
    ran("dns-ascii");
    test_address_conversion();
    ran("inet");
    test_getrusage();
    ran("getrusage");
    test_eventloop_pipe();
    ran("pipe");
    test_synthetic_fd_ftruncate();
    ran("ftruncate");
    test_iocp_blocking_transition();
    ran("iocp");
    test_socket_duplication();
    ran("wsadup");

    if (failures != 0) {
        fprintf(stderr, "%d interop test(s) failed\n", failures);
        return 1;
    }

    printf("interop_smoke: ok (path, errno, UTF-8 FS, pthread, random, LLP64, ASCII, inet, fdapi)\n");
    return 0;
}
