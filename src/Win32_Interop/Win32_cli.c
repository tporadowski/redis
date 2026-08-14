/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#include "Win32_cli.h"

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
        const char *home = getenv("HOME");
        if (home == NULL || home[0] == '\0') {
            const char *up = getenv("USERPROFILE");
            char buf[MAX_PATH];
            if (up == NULL || up[0] == '\0') {
                const char *drive = getenv("HOMEDRIVE");
                const char *path = getenv("HOMEPATH");
                if (drive && path) {
                    snprintf(buf, sizeof(buf), "%s%s", drive, path);
                    up = buf;
                }
            }
            if (up && up[0])
                _putenv_s("HOME", up);
        }
    }
}
