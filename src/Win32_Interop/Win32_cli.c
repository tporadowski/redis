/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#include "Win32_cli.h"
#include "Win32_Error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

void cliWin32Init(void) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if (out != INVALID_HANDLE_VALUE && GetConsoleMode(out, &mode)) {
        SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING |
                                 ENABLE_PROCESSED_OUTPUT);
    }

    /* getDotfilePath() uses $HOME. Windows users have USERPROFILE. */
    {
        char *home = win32_getenv_utf8("HOME");
        if (home == NULL || home[0] == '\0') {
            char *up = win32_getenv_utf8("USERPROFILE");
            char *drive = NULL;
            char *path = NULL;
            char buf[1024];
            const char *value = up;
            if (up == NULL || up[0] == '\0') {
                drive = win32_getenv_utf8("HOMEDRIVE");
                path = win32_getenv_utf8("HOMEPATH");
                if (drive && path) {
                    snprintf(buf, sizeof(buf), "%s%s", drive, path);
                    value = buf;
                }
            }
            if (value && value[0]) {
                wchar_t *wide = win32_utf8_to_wide(value);
                if (wide) {
                    SetEnvironmentVariableW(L"HOME", wide);
                    win32_free(wide);
                }
            }
            win32_free(drive);
            win32_free(path);
            win32_free(up);
        }
        win32_free(home);
    }
}
