/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/* History + non-TTY linenoise path. Console ReadConsole editing needs a TTY. */
#include "linenoise.h"
#include "Win32_Interop/Win32_Error.h"
#include "Win32_Interop/posix/unistd.h"

#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static int failures;

static void check(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

int main(void) {
    char histpath[MAX_PATH];
    char inpath[MAX_PATH];
    char buf[256];
    FILE *fp;
    int infd;
    char *line;
    int saw_set = 0;
    int saw_auth = 0;
    DWORD pid = GetCurrentProcessId();

    snprintf(histpath, sizeof(histpath), "ln-hist-\xc3\xa9-%lu.txt",
             (unsigned long)pid);
    snprintf(inpath, sizeof(inpath), "ln-in-%lu.txt", (unsigned long)pid);

    check(linenoiseHistorySetMaxLen(32) == 1,
          "history max length should be accepted");
    check(linenoiseHistoryAdd("SET foo", 0) == 1,
          "history should accept a normal command");
    check(linenoiseHistoryAdd("AUTH secret", 1) == 1,
          "history should accept a sensitive command");
    check(linenoiseHistorySave(histpath) == 0,
          "history should save to a UTF-8 path");

    fp = replace_fopen(histpath, "r");
    check(fp != NULL, "UTF-8 history file should be readable");
    if (fp) {
        while (fgets(buf, sizeof(buf), fp)) {
            if (strstr(buf, "SET foo")) saw_set = 1;
            if (strstr(buf, "AUTH")) saw_auth = 1;
        }
        fclose(fp);
    }
    check(saw_set, "saved history should contain the normal command");
    check(!saw_auth, "saved history should omit the sensitive command");
    check(replace_unlink(histpath) == 0,
          "UTF-8 history file should be removable");

    fp = fopen(inpath, "wb");
    check(fp != NULL, "non-TTY input file should be created");
    if (fp) {
        fputs("hello from pipe\n", fp);
        fclose(fp);
    }
    infd = _open(inpath, _O_RDONLY);
    check(infd >= 0, "non-TTY input file should open");
    if (infd >= 0) {
        check(_dup2(infd, 0) == 0, "stdin should be redirected");
        _close(infd);
    }
    check(!_isatty(0), "redirected stdin must not be a console");
    line = linenoise("prompt> ");
    check(line != NULL && strcmp(line, "hello from pipe") == 0,
          "non-TTY linenoise should return the redirected line");
    linenoiseFree(line);
    _unlink(inpath);

    if (failures) {
        fprintf(stderr, "%d linenoise test(s) failed\n", failures);
        return 1;
    }
    printf("linenoise_smoke: ok (history UTF-8, sensitive filter, non-TTY read)\n");
    return 0;
}
