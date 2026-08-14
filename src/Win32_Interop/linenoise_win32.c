/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/*
 * Windows linenoise: fgets input + file-backed history.
 * Full line editing can wait; redis-cli interactive mode works without it.
 */

#include "linenoise.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UNUSED
#define UNUSED(V) ((void)(V))
#endif

#define LINENOISE_DEFAULT_MAX 100

static linenoiseCompletionCallback *completion_cb;
static linenoiseHintsCallback *hints_cb;
static linenoiseFreeHintsCallback *free_hints_cb;
static char **hist;
static int hist_len;
static int hist_max = LINENOISE_DEFAULT_MAX;

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
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
        return _strdup(buf);
    }
}

void linenoiseFree(void *ptr) { free(ptr); }

int linenoiseHistoryAdd(const char *line, int is_sensitive) {
    char *copy;
    UNUSED(is_sensitive);
    if (!line || !line[0] || hist_max <= 0) return 0;
    if (hist_len > 0 && hist[hist_len - 1] && strcmp(hist[hist_len - 1], line) == 0)
        return 0;
    if (!hist) {
        hist = (char **)calloc((size_t)hist_max, sizeof(char *));
        if (!hist) return 0;
    }
    if (hist_len == hist_max) {
        free(hist[0]);
        memmove(hist, hist + 1, (size_t)(hist_max - 1) * sizeof(char *));
        hist_len--;
    }
    copy = _strdup(line);
    if (!copy) return 0;
    hist[hist_len++] = copy;
    return 1;
}

int linenoiseHistorySetMaxLen(int len) {
    if (len < 1) return 0;
    hist_max = len;
    return 1;
}

int linenoiseHistorySave(const char *filename) {
    FILE *fp;
    int i;
    if (!filename) return -1;
    fp = fopen(filename, "w");
    if (!fp) return -1;
    for (i = 0; i < hist_len; i++) {
        if (hist[i]) fprintf(fp, "%s\n", hist[i]);
    }
    fclose(fp);
    return 0;
}

int linenoiseHistoryLoad(const char *filename) {
    FILE *fp;
    char buf[4096];
    if (!filename) return -1;
    fp = fopen(filename, "r");
    if (!fp) return -1;
    while (fgets(buf, sizeof(buf), fp)) {
        size_t n = strlen(buf);
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
        if (n) linenoiseHistoryAdd(buf, 0);
    }
    fclose(fp);
    return 0;
}

void linenoiseClearScreen(void) {
    fputs("\x1b[H\x1b[2J", stdout);
    fflush(stdout);
}
void linenoiseSetMultiLine(int ml) { UNUSED(ml); }
void linenoisePrintKeyCodes(void) {}
void linenoiseMaskModeEnable(void) {}
void linenoiseMaskModeDisable(void) {}
