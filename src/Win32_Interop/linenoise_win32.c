/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/*
 * Minimal linenoise for Windows (0.2). Interactive editing is PR 0.3.
 * Provides the API redis-cli links so the first compile succeeds.
 */

#include "linenoise.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UNUSED
#define UNUSED(V) ((void)(V))
#endif

static linenoiseCompletionCallback *completion_cb;
static linenoiseHintsCallback *hints_cb;
static linenoiseFreeHintsCallback *free_hints_cb;

void linenoiseSetCompletionCallback(linenoiseCompletionCallback *fn) {
    completion_cb = fn;
}
void linenoiseSetHintsCallback(linenoiseHintsCallback *fn) { hints_cb = fn; }
void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback *fn) {
    free_hints_cb = fn;
}
void linenoiseAddCompletion(linenoiseCompletions *lc, const char *str) {
    UNUSED(lc);
    UNUSED(str);
}

char *linenoise(const char *prompt) {
    char buf[4096];
    if (prompt && *prompt) fputs(prompt, stdout);
    fflush(stdout);
    if (!fgets(buf, sizeof(buf), stdin)) return NULL;
    {
        size_t n = strlen(buf);
        if (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
        if (n && buf[n - 1] == '\r') buf[--n] = 0;
        return _strdup(buf);
    }
}

void linenoiseFree(void *ptr) { free(ptr); }
int linenoiseHistoryAdd(const char *line, int is_sensitive) {
    UNUSED(line);
    UNUSED(is_sensitive);
    return 0;
}
int linenoiseHistorySetMaxLen(int len) {
    UNUSED(len);
    return 0;
}
int linenoiseHistorySave(const char *filename) {
    UNUSED(filename);
    return 0;
}
int linenoiseHistoryLoad(const char *filename) {
    UNUSED(filename);
    return 0;
}
void linenoiseClearScreen(void) {}
void linenoiseSetMultiLine(int ml) { UNUSED(ml); }
void linenoisePrintKeyCodes(void) {}
void linenoiseMaskModeEnable(void) {}
void linenoiseMaskModeDisable(void) {}
