/* linenoise.c -- guerrilla line editing library against the idea that a
 * line editing lib needs to be 20,000 lines of C code.
 *
 * You can find the latest source code at:
 *
 *   http://github.com/antirez/linenoise
 *
 * Does a number of crazy assumptions that happen to be true in 99.9999% of
 * the 2010 UNIX computers around.
 *
 * ------------------------------------------------------------------------
 *
 * Windows console path (ReadConsoleInputW, UTF-8 editing, FAKETTY pipe
 * mode) taken from the pwin32 MinGW 8.10 tree (deps/linenoise/linenoise.c)
 * and adapted for this CMake/clang-cl port. Official linenoise remains
 * the POSIX implementation; do not drop the Windows blocks when
 * refreshing from upstream.
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010-2016, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2013, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ------------------------------------------------------------------------
 *
 * References:
 * - http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 * - http://www.3waylabs.com/nw/WWW/products/wizcon/vt220.html
 *
 * Todo list:
 * - Filter bogus Ctrl+<char> combinations.
 * - Win32 support
 *
 * Bloat:
 * - History search like Ctrl+r in readline?
 *
 * List of escape sequences used by this program, we do everything just
 * with three sequences. In order to be so cheap we may have some
 * flickering effect with some slow terminal, but the lesser sequences
 * the more compatible.
 *
 * EL (Erase Line)
 *    Sequence: ESC [ n K
 *    Effect: if n is 0 or missing, clear from cursor to end of line
 *    Effect: if n is 1, clear from beginning of line to cursor
 *    Effect: if n is 2, clear entire line
 *
 * CUF (CUrsor Forward)
 *    Sequence: ESC [ n C
 *    Effect: moves cursor forward n chars
 *
 * CUB (CUrsor Backward)
 *    Sequence: ESC [ n D
 *    Effect: moves cursor backward n chars
 *
 * The following is used to get the terminal width if getting
 * the width with the TIOCGWINSZ ioctl fails
 *
 * DSR (Device Status Report)
 *    Sequence: ESC [ 6 n
 *    Effect: reports the current cusor position as ESC [ n ; m R
 *            where n is the row and m is the column
 *
 * When multi line mode is enabled, we also use an additional escape
 * sequence. However multi line editing is disabled by default.
 *
 * CUU (Cursor Up)
 *    Sequence: ESC [ n A
 *    Effect: moves cursor up of n chars.
 *
 * CUD (Cursor Down)
 *    Sequence: ESC [ n B
 *    Effect: moves cursor down of n chars.
 *
 * When linenoiseClearScreen() is called, two additional escape sequences
 * are used in order to clear the screen and position the cursor at home
 * position.
 *
 * CUP (Cursor position)
 *    Sequence: ESC [ H
 *    Effect: moves the cursor to upper left corner
 *
 * ED (Erase display)
 *    Sequence: ESC [ 2 J
 *    Effect: clear the whole screen
 *
 */

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include "Win32_Error.h"
#ifndef UNUSED
#define UNUSED(V) ((void) V)
#endif
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif
#ifndef write
#define write _write
#define read _read
#define isatty _isatty
#endif
#ifndef strdup
#define strdup _strdup
#endif
#ifndef S_IRUSR
#define S_IRUSR _S_IREAD
#define S_IWUSR _S_IWRITE
#endif
#ifndef ENOTTY
#define ENOTTY 25
#endif
FILE *replace_fopen(const char *path, const char *mode);
int replace_chmod(const char *path, int mode);
#else
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef _WIN32
#include <sys/ioctl.h>
#include <unistd.h>
#endif
#include <assert.h>
#include "linenoise.h"

#define SEQ_BUFFER_MAX_LENGTH 8
#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_MAX_LINE 4096
#ifndef _WIN32
static char *unsupported_term[] = {"dumb","cons25","emacs",NULL};
#endif
static linenoiseCompletionCallback *completionCallback = NULL;
static linenoiseHintsCallback *hintsCallback = NULL;
static linenoiseFreeHintsCallback *freeHintsCallback = NULL;

#ifndef _WIN32
static struct termios orig_termios; /* In order to restore at exit.*/
#endif
static int maskmode = 0; /* Show "***" instead of input. For passwords. */
static int rawmode = 0; /* For atexit() function to check if restore is needed*/
static int mlmode = 0;  /* Multi line mode. Default is single line. */
static int atexit_registered = 0; /* Register atexit just 1 time. */
static int history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;
static char **history = NULL;
static int *history_sensitive = NULL; /* An array records whether each line in
                                       * history is sensitive. */

static int reverse_search_mode_enabled = 0;
static int reverse_search_direction = 0; /* 1 means forward, -1 means backward. */
static int cycle_to_next_search = 0; /* indicates whether to continue the search with CTRL+S or CTRL+R. */
static char search_result[LINENOISE_MAX_LINE];
static char search_result_friendly[LINENOISE_MAX_LINE];
static int search_result_history_index = 0;
static int search_result_start_offset = 0;
static int ignore_once_hint = 0; /* Flag to ignore hint once, preventing it from interfering
                                  * with search results right after exiting search mode. */

/* The linenoiseState structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */
struct linenoiseState {
    int ifd;            /* Terminal stdin file descriptor. */
    int ofd;            /* Terminal stdout file descriptor. */
    char *buf;          /* Edited line buffer. */
    size_t buflen;      /* Edited line buffer size. */
    const char *origin_prompt; /* Original prompt, used to restore when exiting search mode. */
    const char *prompt; /* Prompt to display. */
    size_t plen;        /* Prompt length. */
    size_t pos;         /* Current cursor position. */
    size_t oldpos;      /* Previous refresh cursor position. */
    size_t len;         /* Current edited line length. */
    size_t cols;        /* Number of columns in terminal. */
    size_t maxrows;     /* Maximum num of rows used so far (multiline mode) */
    int history_index;  /* The history index we are currently editing. */
};

typedef struct {
    int len;                /* Length of the result string. */
    char *result;           /* Search result string. */
    int search_term_index;  /* Position of the search term in the history record. */
    int search_term_len;    /* Length of the search term. */
} linenoiseHistorySearchResult;

enum KEY_ACTION{
	KEY_NULL = 0,	    /* NULL */
	CTRL_A = 1,         /* Ctrl+a */
	CTRL_B = 2,         /* Ctrl-b */
	CTRL_C = 3,         /* Ctrl-c */
	CTRL_D = 4,         /* Ctrl-d */
	CTRL_E = 5,         /* Ctrl-e */
	CTRL_F = 6,         /* Ctrl-f */
	CTRL_G = 7,         /* Ctrl-g */
	CTRL_H = 8,         /* Ctrl-h */
	TAB = 9,            /* Tab */
	NL = 10,            /* Enter typed before raw mode was enabled */
	CTRL_K = 11,        /* Ctrl+k */
	CTRL_L = 12,        /* Ctrl+l */
	ENTER = 13,         /* Enter */
	CTRL_N = 14,        /* Ctrl-n */
	CTRL_P = 16,        /* Ctrl-p */
	CTRL_R = 18,        /* Ctrl-r */
	CTRL_S = 19,        /* Ctrl-s */
	CTRL_T = 20,        /* Ctrl-t */
	CTRL_U = 21,        /* Ctrl+u */
	CTRL_W = 23,        /* Ctrl+w */
	ESC = 27,           /* Escape */
	BACKSPACE =  127    /* Backspace */
};

#ifdef _WIN32
enum WIN32_KEY_ACTION {
    WIN32_KEY_NONE = 0,
    WIN32_KEY_DELETE,
    WIN32_KEY_WORD_LEFT,
    WIN32_KEY_WORD_RIGHT
};
#endif

static void linenoiseAtExit(void);
int linenoiseHistoryAdd(const char *line, int is_sensitive);
static void refreshLine(struct linenoiseState *l);
static void refreshSearchResult(struct linenoiseState *ls);

static inline void resetSearchResult(void) {
    memset(search_result, 0, sizeof(search_result));
    memset(search_result_friendly, 0, sizeof(search_result_friendly));
}

#ifdef _WIN32
#ifndef STDIN_FILENO
    #define STDIN_FILENO (_fileno(stdin))
#endif

static HANDLE hOut = INVALID_HANDLE_VALUE;
static HANDLE hIn = INVALID_HANDLE_VALUE;
static DWORD original_console_mode;
static int fake_tty_mode_active;
static int fake_tty_original_mode;

static int win32Utf8Encode(unsigned int codepoint, char *bytes, size_t capacity) {
    if (codepoint <= 0x7f && capacity >= 1) {
        bytes[0] = (char)codepoint;
        return 1;
    }
    if (codepoint <= 0x7ff && capacity >= 2) {
        bytes[0] = (char)(0xc0 | (codepoint >> 6));
        bytes[1] = (char)(0x80 | (codepoint & 0x3f));
        return 2;
    }
    if (codepoint <= 0xffff && capacity >= 3) {
        bytes[0] = (char)(0xe0 | (codepoint >> 12));
        bytes[1] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        bytes[2] = (char)(0x80 | (codepoint & 0x3f));
        return 3;
    }
    if (codepoint <= 0x10ffff && capacity >= 4) {
        bytes[0] = (char)(0xf0 | (codepoint >> 18));
        bytes[1] = (char)(0x80 | ((codepoint >> 12) & 0x3f));
        bytes[2] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        bytes[3] = (char)(0x80 | (codepoint & 0x3f));
        return 4;
    }
    return 0;
}

static int win32read(char *bytes, size_t capacity, int *key_action) {
    static WCHAR high_surrogate;
    static KEY_EVENT_RECORD repeated_event;
    static WORD repeats_remaining;
    static KEY_EVENT_RECORD replay_event;
    static BOOL replay_valid;
    DWORD count;
    INPUT_RECORD record;

    while (1) {
        KEY_EVENT_RECORD event;
        BOOL altgr;
        unsigned int codepoint;

        *key_action = WIN32_KEY_NONE;
        if (replay_valid) {
            event = replay_event;
            replay_valid = FALSE;
        } else if (repeats_remaining != 0) {
            event = repeated_event;
            repeats_remaining--;
        } else {
            if (!ReadConsoleInputW(hIn, &record, 1, &count)) {
                errno = win32_errno_from_system_error((int)GetLastError());
                if (errno == 0) errno = EIO;
                return -1;
            }
            if (!count) {
                errno = EIO;
                return -1;
            }
            if (record.EventType != KEY_EVENT ||
                !record.Event.KeyEvent.bKeyDown)
                continue;
            event = repeated_event = record.Event.KeyEvent;
            repeats_remaining = event.wRepeatCount > 1 ?
                                event.wRepeatCount - 1 : 0;
        }

        codepoint = event.uChar.UnicodeChar;
        if (high_surrogate &&
            !(codepoint >= 0xdc00 && codepoint <= 0xdfff)) {
            replay_event = event;
            replay_valid = TRUE;
            high_surrogate = 0;
            return win32Utf8Encode(0xfffd, bytes, capacity);
        }

        altgr = (event.dwControlKeyState & LEFT_CTRL_PRESSED) != 0 &&
                (event.dwControlKeyState & RIGHT_ALT_PRESSED) != 0;

        if ((event.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) &&
            !altgr)
        {
            high_surrogate = 0;
            switch (event.wVirtualKeyCode) {
            case 'A': bytes[0] = CTRL_A; return 1;
            case 'B': bytes[0] = CTRL_B; return 1;
            case 'C': bytes[0] = CTRL_C; return 1;
            case 'D': bytes[0] = CTRL_D; return 1;
            case 'E': bytes[0] = CTRL_E; return 1;
            case 'F': bytes[0] = CTRL_F; return 1;
            case 'G': bytes[0] = CTRL_G; return 1;
            case 'H': bytes[0] = CTRL_H; return 1;
            case 'K': bytes[0] = CTRL_K; return 1;
            case 'L': bytes[0] = CTRL_L; return 1;
            case 'N': bytes[0] = CTRL_N; return 1;
            case 'P': bytes[0] = CTRL_P; return 1;
            case 'R': bytes[0] = CTRL_R; return 1;
            case 'S': bytes[0] = CTRL_S; return 1;
            case 'T': bytes[0] = CTRL_T; return 1;
            case 'U': bytes[0] = CTRL_U; return 1;
            case 'W': bytes[0] = CTRL_W; return 1;
            case VK_LEFT:  *key_action = WIN32_KEY_WORD_LEFT; return 1;
            case VK_RIGHT: *key_action = WIN32_KEY_WORD_RIGHT; return 1;
            default: continue;
            }
        }

        switch (event.wVirtualKeyCode) {
        case VK_ESCAPE: high_surrogate = 0; bytes[0] = CTRL_C; return 1;
        case VK_RETURN: high_surrogate = 0; bytes[0] = ENTER; return 1;
        case VK_LEFT:   high_surrogate = 0; bytes[0] = CTRL_B; return 1;
        case VK_RIGHT:  high_surrogate = 0; bytes[0] = CTRL_F; return 1;
        case VK_UP:     high_surrogate = 0; bytes[0] = CTRL_P; return 1;
        case VK_DOWN:   high_surrogate = 0; bytes[0] = CTRL_N; return 1;
        case VK_HOME:   high_surrogate = 0; bytes[0] = CTRL_A; return 1;
        case VK_END:    high_surrogate = 0; bytes[0] = CTRL_E; return 1;
        case VK_BACK:   high_surrogate = 0; bytes[0] = CTRL_H; return 1;
        case VK_DELETE: high_surrogate = 0; *key_action = WIN32_KEY_DELETE; return 1;
        default: break;
        }

        if (!codepoint) continue;
        if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
            if (high_surrogate) {
                replay_event = event;
                replay_valid = TRUE;
                high_surrogate = 0;
                return win32Utf8Encode(0xfffd, bytes, capacity);
            }
            high_surrogate = (WCHAR)codepoint;
            continue;
        }
        if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
            if (high_surrogate) {
                codepoint = 0x10000 +
                    (((unsigned int)high_surrogate - 0xd800) << 10) +
                    (codepoint - 0xdc00);
                high_surrogate = 0;
            } else {
                codepoint = 0xfffd;
            }
        } else {
            high_surrogate = 0;
        }
        return win32Utf8Encode(codepoint, bytes, capacity);
    }
}
#endif

/* Linenoise stores UTF-8 byte offsets, but editing and terminal positioning
 * must advance by complete Unicode scalar values. Invalid input bytes remain
 * independently editable instead of making the line buffer unusable. */
static size_t utf8CharLen(const char *s, size_t len) {
    const unsigned char *p = (const unsigned char *)s;
    unsigned int codepoint;
    size_t needed;

    if (len == 0) return 0;
    if (p[0] < 0x80) return 1;
    if (p[0] >= 0xc2 && p[0] <= 0xdf) {
        codepoint = p[0] & 0x1f;
        needed = 2;
    } else if (p[0] >= 0xe0 && p[0] <= 0xef) {
        codepoint = p[0] & 0x0f;
        needed = 3;
    } else if (p[0] >= 0xf0 && p[0] <= 0xf4) {
        codepoint = p[0] & 0x07;
        needed = 4;
    } else {
        return 1;
    }
    if (needed > len) return 1;
    for (size_t i = 1; i < needed; i++) {
        if ((p[i] & 0xc0) != 0x80) return 1;
        codepoint = (codepoint << 6) | (p[i] & 0x3f);
    }
    if ((needed == 2 && codepoint < 0x80) ||
        (needed == 3 && codepoint < 0x800) ||
        (needed == 4 && codepoint < 0x10000) ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff) ||
        codepoint > 0x10ffff)
        return 1;
    return needed;
}

static size_t utf8NextChar(const char *s, size_t len, size_t pos) {
    size_t charlen;
    if (pos >= len) return len;
    charlen = utf8CharLen(s + pos, len - pos);
    return pos + (charlen ? charlen : 1);
}

static size_t utf8PrevChar(const char *s, size_t pos) {
    size_t candidate;
    if (pos == 0) return 0;
    candidate = pos - 1;
    while (candidate > 0 && pos - candidate < 4 &&
           (((unsigned char)s[candidate] & 0xc0) == 0x80))
        candidate--;
    if (candidate + utf8CharLen(s + candidate, pos - candidate) == pos)
        return candidate;
    return pos - 1;
}

static size_t utf8Columns(const char *s, size_t len) {
    size_t columns = 0;
    size_t pos = 0;
    while (pos < len) {
        pos = utf8NextChar(s, len, pos);
        columns++;
    }
    return columns;
}

static size_t utf8BytesForColumns(const char *s, size_t len, size_t columns) {
    size_t pos = 0;
    while (pos < len && columns--)
        pos = utf8NextChar(s, len, pos);
    return pos;
}

/* Debugging macro. */
#if 0
FILE *lndebug_fp = NULL;
#define lndebug(...) \
    do { \
        if (lndebug_fp == NULL) { \
            lndebug_fp = fopen("/tmp/lndebug.txt","a"); \
            fprintf(lndebug_fp, \
            "[%d %d %d] p: %d, rows: %d, rpos: %d, max: %d, oldmax: %d\n", \
            (int)l->len,(int)l->pos,(int)l->oldpos,plen,rows,rpos, \
            (int)l->maxrows,old_rows); \
        } \
        fprintf(lndebug_fp, ", " __VA_ARGS__); \
        fflush(lndebug_fp); \
    } while (0)
#else
#define lndebug(fmt, ...)
#endif

/* ======================= Low level terminal handling ====================== */

/* Enable "mask mode". When it is enabled, instead of the input that
 * the user is typing, the terminal will just display a corresponding
 * number of asterisks, like "****". This is useful for passwords and other
 * secrets that should not be displayed. */
void linenoiseMaskModeEnable(void) {
    maskmode = 1;
}

/* Disable mask mode. */
void linenoiseMaskModeDisable(void) {
    maskmode = 0;
}

/* Set if to use or not the multi line mode. */
void linenoiseSetMultiLine(int ml) {
    mlmode = ml;
}

#define REVERSE_SEARCH_PROMPT(direction) ((direction) == -1 ? "(reverse-i-search): " : "(i-search): ")

/* Enables the reverse search mode and refreshes the prompt. */
static void enableReverseSearchMode(struct linenoiseState *l) {
    assert(reverse_search_mode_enabled != 1);
    reverse_search_mode_enabled = 1;
    l->origin_prompt = l->prompt;
    l->prompt = REVERSE_SEARCH_PROMPT(reverse_search_direction);
    refreshLine(l);
}

/* This function disables the reverse search mode and returns the terminal to its original state.
 * If the 'discard' parameter is true, it discards the user's input search keyword and search result.
 * Otherwise, it copies the search result into 'buf', If there is no search result, it copies the
 * input search keyword instead. */
static void disableReverseSearchMode(struct linenoiseState *l, char *buf, size_t buflen, int discard) {
    if (discard) {
        buf[0] = '\0';
        l->pos = l->len = 0;
    } else {
        ignore_once_hint = 1;
        if (strlen(search_result)) {
            strncpy(buf, search_result, buflen);
            buf[buflen-1] = '\0';
            l->pos = l->len = strlen(buf);
        }
    }

    /* Reset the state to non-search state. */
    reverse_search_mode_enabled = 0;
    l->prompt = l->origin_prompt;
    resetSearchResult();
    refreshLine(l);
}

/* Return true if the terminal name is in the list of terminals we know are
 * not able to understand basic escape sequences. */
static int isUnsupportedTerm(void) {
#ifndef _WIN32
    char *term = getenv("TERM");
    int j;

    if (term == NULL) return 0;
    for (j = 0; unsupported_term[j]; j++)
        if (!strcasecmp(term,unsupported_term[j])) return 1;
#endif
    return 0;
}

/* Raw mode: 1960's magic. */
static int enableRawMode(int fd) {
#ifndef _WIN32
    if (getenv("FAKETTY_WITH_PROMPT") != NULL) {
        return 0;
    }
    struct termios raw;

    if (!isatty(STDIN_FILENO)) goto fatal;
    if (!atexit_registered) {
        atexit(linenoiseAtExit);
        atexit_registered = 1;
    }
    if (tcgetattr(fd,&orig_termios) == -1) goto fatal;

    raw = orig_termios;  /* modify the original mode */
    /* input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* output modes - disable post processing */
    raw.c_oflag &= ~(OPOST);
    /* control modes - set 8 bit chars */
    raw.c_cflag |= (CS8);
    /* local modes - choing off, canonical off, no extended functions,
     * no signal chars (^Z,^C) */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* control chars - set return condition: min number of bytes and timer.
     * We want read to return every single byte, without timeout. */
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

    /* put terminal in raw mode */
    if (tcsetattr(fd,TCSANOW,&raw) < 0) goto fatal;
    rawmode = 1;
#else
    /* The Redis CLI integration tests drive linenoise through a pipe. Keep
     * native console handling for real interactive sessions, but let the
     * explicitly requested fake-TTY mode use ordinary file descriptor I/O. */
    if (getenv("FAKETTY_WITH_PROMPT") != NULL) {
        /* Preserve carriage returns as ENTER. In CRT text mode a CRLF pair
         * becomes a bare LF, which linenoise intentionally ignores while it
         * is emulating raw terminal input. */
        fake_tty_original_mode = _setmode(fd, _O_BINARY);
        if (fake_tty_original_mode == -1) goto fatal;
        fake_tty_mode_active = 1;
        rawmode = 1;
        if (!atexit_registered) {
            atexit(linenoiseAtExit);
            atexit_registered = 1;
        }
        return 0;
    }

    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn == NULL || hIn == INVALID_HANDLE_VALUE ||
        !GetConsoleMode(hIn, &original_console_mode))
        goto fatal;
    {
        DWORD raw_console_mode = original_console_mode;
        raw_console_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT |
                              ENABLE_PROCESSED_INPUT |
                              ENABLE_QUICK_EDIT_MODE);
#ifdef ENABLE_VIRTUAL_TERMINAL_INPUT
        raw_console_mode &= ~ENABLE_VIRTUAL_TERMINAL_INPUT;
#endif
        raw_console_mode |= ENABLE_EXTENDED_FLAGS;
        if (!SetConsoleMode(hIn, raw_console_mode)) goto fatal;
    }

    if (!atexit_registered) {
        atexit(linenoiseAtExit);
        atexit_registered = 1;
    }

    rawmode = 1;
#endif
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

static void disableRawMode(int fd) {
#ifdef _WIN32
    if (!rawmode) return;
    if (fake_tty_mode_active) {
        _setmode(fd, fake_tty_original_mode);
        fake_tty_mode_active = 0;
    } else if (hIn != NULL && hIn != INVALID_HANDLE_VALUE) {
        SetConsoleMode(hIn, original_console_mode);
    }
    rawmode = 0;
#else
    /* Don't even check the return value as it's too late. */
    if (rawmode && tcsetattr(fd,TCSANOW,&orig_termios) != -1)
        rawmode = 0;
#endif
}

#ifndef _WIN32
/* Use the ESC [6n escape sequence to query the horizontal cursor position
 * and return it. On error -1 is returned, on success the position of the
 * cursor. */
static int getCursorPosition(int ifd, int ofd) {
    char buf[32];
    int cols, rows;
    unsigned int i = 0;

    /* Report cursor location */
    if (write(ofd, "\x1b[6n", 4) != 4) return -1;

    /* Read the response: ESC [ rows ; cols R */
    while (i < sizeof(buf)-1) {
        if (read(ifd,buf+i,1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    /* Parse it. */
    if (buf[0] != ESC || buf[1] != '[') return -1;
    if (sscanf(buf+2,"%d;%d",&rows,&cols) != 2) return -1;
    return cols;
}
#endif

/* Try to get the number of columns in the current terminal, or assume 80
 * if it fails. */
static int getColumns(int ifd, int ofd) {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO b;

    if (getenv("FAKETTY_WITH_PROMPT") != NULL) return 80;

    if (hOut == NULL || hOut == INVALID_HANDLE_VALUE ||
        !GetConsoleScreenBufferInfo(hOut, &b))
        return 80;
    return b.srWindow.Right - b.srWindow.Left + 1;
#else
    if (getenv("FAKETTY_WITH_PROMPT") != NULL) {
        goto failed;
    }
    struct winsize ws;

    if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        /* ioctl() failed. Try to query the terminal itself. */
        int start, cols;

        /* Get the initial position so we can restore it later. */
        start = getCursorPosition(ifd,ofd);
        if (start == -1) goto failed;

        /* Go to right margin and get position. */
        if (write(ofd,"\x1b[999C",6) != 6) goto failed;
        cols = getCursorPosition(ifd,ofd);
        if (cols == -1) goto failed;

        /* Restore position. */
        if (cols > start) {
            char seq[32];
            snprintf(seq,32,"\x1b[%dD",cols-start);
            if (write(ofd,seq,strlen(seq)) == -1) {
                /* Can't recover... */
            }
        }
        return cols;
    } else {
        return ws.ws_col;
    }

failed:
    return 80;
#endif
}

/* Clear the screen. Used to handle ctrl+l */
void linenoiseClearScreen(void) {
    if (write(STDOUT_FILENO,"\x1b[H\x1b[2J",7) <= 0) {
        /* nothing to do, just to avoid warning. */
    }
}

/* Beep, used for completion when there is nothing to complete or when all
 * the choices were already shown. */
static void linenoiseBeep(void) {
    fprintf(stderr, "\x7");
    fflush(stderr);
}

/* ============================== Completion ================================ */

/* Free a list of completion option populated by linenoiseAddCompletion(). */
static void freeCompletions(linenoiseCompletions *lc) {
    size_t i;
    for (i = 0; i < lc->len; i++)
        free(lc->cvec[i]);
    if (lc->cvec != NULL)
        free(lc->cvec);
}

/* This is an helper function for linenoiseEdit() and is called when the
 * user types the <tab> key in order to complete the string currently in the
 * input.
 *
 * The state of the editing is encapsulated into the pointed linenoiseState
 * structure as described in the structure definition. */
static int completeLine(struct linenoiseState *ls, char *input, size_t input_capacity,
                        int *win32_key_action) {
    linenoiseCompletions lc = { 0, NULL };
    int nread = 0, nwritten;
    char c;

    if (input_capacity == 0) return -1;
    input[0] = 0;
#ifdef _WIN32
    *win32_key_action = WIN32_KEY_NONE;
#else
    (void)win32_key_action;
#endif

    completionCallback(ls->buf,&lc);
    if (lc.len == 0) {
        linenoiseBeep();
    } else {
        size_t stop = 0, i = 0;

        while(!stop) {
            /* Show completion or original buffer */
            if (i < lc.len) {
                struct linenoiseState saved = *ls;

                ls->len = ls->pos = strlen(lc.cvec[i]);
                ls->buf = lc.cvec[i];
                refreshLine(ls);
                ls->len = saved.len;
                ls->pos = saved.pos;
                ls->buf = saved.buf;
            } else {
                refreshLine(ls);
            }

#ifdef _WIN32
            if (getenv("FAKETTY_WITH_PROMPT") != NULL)
                nread = (int)read(ls->ifd,input,1);
            else
                nread = win32read(input,input_capacity,win32_key_action);
#else
            nread = (int)read(ls->ifd,input,1);
#endif
            if (nread <= 0) {
                freeCompletions(&lc);
                return -1;
            }
#ifdef _WIN32
            if (*win32_key_action != WIN32_KEY_NONE) {
                stop = 1;
                continue;
            }
#endif
            c = input[0];

            switch(nread == 1 ? c : 0) {
                case 9: /* tab */
                    i = (i+1) % (lc.len+1);
                    if (i == lc.len) linenoiseBeep();
                    break;
                case 27: /* escape */
                    /* Re-show original buffer */
                    if (i < lc.len) refreshLine(ls);
                    stop = 1;
                    break;
                default:
                    /* Update buffer and return */
                    if (i < lc.len) {
                        nwritten = snprintf(ls->buf,ls->buflen,"%s",lc.cvec[i]);
                        ls->len = ls->pos = nwritten;
                    }
                    stop = 1;
                    break;
            }
        }
    }

    freeCompletions(&lc);
    return nread; /* Return the number of bytes in the last input character. */
}

/* Register a callback function to be called for tab-completion. */
void linenoiseSetCompletionCallback(linenoiseCompletionCallback *fn) {
    completionCallback = fn;
}

/* Register a hits function to be called to show hits to the user at the
 * right of the prompt. */
void linenoiseSetHintsCallback(linenoiseHintsCallback *fn) {
    hintsCallback = fn;
}

/* Register a function to free the hints returned by the hints callback
 * registered with linenoiseSetHintsCallback(). */
void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback *fn) {
    freeHintsCallback = fn;
}

/* This function is used by the callback function registered by the user
 * in order to add completion options given the input string when the
 * user typed <tab>. See the example.c source code for a very easy to
 * understand example. */
void linenoiseAddCompletion(linenoiseCompletions *lc, const char *str) {
    size_t len = strlen(str);
    char *copy, **cvec;

    copy = malloc(len+1);
    if (copy == NULL) return;
    memcpy(copy,str,len+1);
    cvec = realloc(lc->cvec,sizeof(char*)*(lc->len+1));
    if (cvec == NULL) {
        free(copy);
        return;
    }
    lc->cvec = cvec;
    lc->cvec[lc->len++] = copy;
}

/* =========================== Line editing ================================= */

/* We define a very simple "append buffer" structure, that is an heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */
struct abuf {
    char *b;
    int len;
};

static void abInit(struct abuf *ab) {
    ab->b = NULL;
    ab->len = 0;
}

static void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b,ab->len+len);

    if (new == NULL) return;
    memcpy(new+ab->len,s,len);
    ab->b = new;
    ab->len += len;
}

static void abFree(struct abuf *ab) {
    free(ab->b);
}

/* Helper of refreshSingleLine() and refreshMultiLine() to show hints
 * to the right of the prompt. */
void refreshShowHints(struct abuf *ab, struct linenoiseState *l, size_t plen) {
    char seq[64];
    size_t line_columns = utf8Columns(l->buf,l->len);

    /* Show hits when not in reverse search mode and not instructed to ignore once. */
    if (reverse_search_mode_enabled || ignore_once_hint) {
        ignore_once_hint = 0;
        return;
    }

    if (hintsCallback && plen+line_columns < l->cols) {
        int color = -1, bold = 0;
        char *hint = hintsCallback(l->buf,&color,&bold);
        if (hint) {
            size_t hintlen = strlen(hint);
            size_t hintmaxlen = l->cols-(plen+line_columns);
            hintlen = utf8BytesForColumns(hint,hintlen,hintmaxlen);
            if (bold == 1 && color == -1) color = 37;
            if (color != -1 || bold != 0)
                snprintf(seq,64,"\033[%d;%d;49m",bold,color);
            else
                seq[0] = '\0';
            abAppend(ab,seq,strlen(seq));
            abAppend(ab,hint,(int)hintlen);
            if (color != -1 || bold != 0)
                abAppend(ab,"\033[0m",4);
            /* Call the function to free the hint returned. */
            if (freeHintsCallback) freeHintsCallback(hint);
        }
    }
}

/* Single line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static void refreshSingleLine(struct linenoiseState *l) {
    char seq[64];
    size_t plen = utf8Columns(l->prompt,strlen(l->prompt));
    int fd = l->ofd;
    size_t start = 0;
    size_t end = l->len;
    size_t pos = utf8Columns(l->buf,l->pos);
    size_t visible_columns;
    struct abuf ab;

    while((plen+pos) >= l->cols && start < l->pos) {
        start = utf8NextChar(l->buf,l->len,start);
        pos--;
    }
    visible_columns = utf8Columns(l->buf+start,end-start);
    while (plen+visible_columns > l->cols && end > start) {
        end = utf8PrevChar(l->buf,end);
        visible_columns--;
    }

    abInit(&ab);
    /* Cursor to left edge */
    snprintf(seq,64,"\r");
    abAppend(&ab,seq,(int)strlen(seq));
    /* Write the prompt and the current buffer content */
    abAppend(&ab,l->prompt,strlen(l->prompt));
    if (maskmode == 1) {
        while (visible_columns--) abAppend(&ab,"*",1);
    } else {
        abAppend(&ab,l->buf+start,(int)(end-start));
    }
    /* Show hits if any. */
    refreshShowHints(&ab,l,plen);
    /* Erase to right */
    snprintf(seq,64,"\x1b[0K");
    abAppend(&ab,seq,(int)strlen(seq));
    /* Move cursor to original position. */
    snprintf(seq,64,"\r\x1b[%dC", (int)(pos+plen));
    abAppend(&ab,seq,(int)strlen(seq));
    if (write(fd,ab.b,ab.len) == -1) {} /* Can't recover from write error. */
    abFree(&ab);
}

/* Multi line low level line refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static void refreshMultiLine(struct linenoiseState *l) {
    char seq[64];
    size_t plen = utf8Columns(l->prompt,strlen(l->prompt));
    size_t len = utf8Columns(l->buf,l->len);
    size_t oldpos = utf8Columns(l->buf,l->oldpos);
    size_t pos = utf8Columns(l->buf,l->pos);
    int rows = (int)((plen+len+l->cols-1)/l->cols); /* rows used by current buf. */
    int rpos = (int)((plen+oldpos+l->cols)/l->cols); /* cursor relative row. */
    int rpos2; /* rpos after refresh. */
    int col; /* colum position, zero-based. */
    int old_rows = (int)l->maxrows;
    int fd = l->ofd, j;
    struct abuf ab;

    /* Update maxrows if needed. */
    if (rows > (int)l->maxrows) l->maxrows = rows;

    /* First step: clear all the lines used before. To do so start by
     * going to the last row. */
    abInit(&ab);
    if (old_rows-rpos > 0) {
        lndebug("go down %d", old_rows-rpos);
        snprintf(seq,64,"\x1b[%dB", old_rows-rpos);
        abAppend(&ab,seq,(int)strlen(seq));
    }

    /* Now for every row clear it, go up. */
    for (j = 0; j < old_rows-1; j++) {
        lndebug("clear+up");
        snprintf(seq,64,"\r\x1b[0K\x1b[1A");
        abAppend(&ab,seq,(int)strlen(seq));
    }

    /* Clean the top line. */
    lndebug("clear");
    snprintf(seq,64,"\r\x1b[0K");
    abAppend(&ab,seq,(int)strlen(seq));

    /* Write the prompt and the current buffer content */
    abAppend(&ab,l->prompt,strlen(l->prompt));
    if (maskmode == 1) {
        size_t i;
        for (i = 0; i < len; i++) abAppend(&ab,"*",1);
    } else {
        refreshSearchResult(l);
        if (strlen(search_result) > 0) {
            abAppend(&ab, search_result_friendly, strlen(search_result_friendly));
        } else {
            abAppend(&ab,l->buf,l->len);
        }
    }

    /* Show hits if any. */
    refreshShowHints(&ab,l,plen);

    /* If we are at the very end of the screen with our prompt, we need to
     * emit a newline and move the prompt to the first column. */
    if (l->pos &&
        l->pos == l->len &&
        (pos+plen) % l->cols == 0)
    {
        lndebug("<newline>");
        abAppend(&ab,"\n",1);
        snprintf(seq,64,"\r");
        abAppend(&ab,seq,(int)strlen(seq));
        rows++;
        if (rows > (int)l->maxrows) l->maxrows = rows;
    }

    /* Move cursor to right position. */
    rpos2 = (int)((plen+pos+l->cols)/l->cols); /* current cursor relative row. */
    lndebug("rpos2 %d", rpos2);

    /* Go up till we reach the expected position. */
    if (rows-rpos2 > 0) {
        lndebug("go-up %d", rows-rpos2);
        snprintf(seq,64,"\x1b[%dA", rows-rpos2);
        abAppend(&ab,seq,(int)strlen(seq));
    }

    /* Set column. */
    col = (int)((plen+pos) % l->cols);
    if (strlen(search_result) > 0) {
        col += (int)utf8Columns(search_result,(size_t)search_result_start_offset);
    }
    lndebug("set col %d", 1+col);
    if (col)
        snprintf(seq,64,"\r\x1b[%dC", col);
    else
        snprintf(seq,64,"\r");
    abAppend(&ab,seq,(int)strlen(seq));

    lndebug("\n");
    l->oldpos = l->pos;

    if (write(fd,ab.b,ab.len) == -1) {} /* Can't recover from write error. */
    abFree(&ab);
}

/* Calls the two low level functions refreshSingleLine() or
 * refreshMultiLine() according to the selected mode. */
static void refreshLine(struct linenoiseState *l) {
    if (mlmode)
        refreshMultiLine(l);
    else
        refreshSingleLine(l);
}

/* Insert one complete input character at the current UTF-8 byte offset. */
static int linenoiseEditInsertBytes(struct linenoiseState *l,
                                    const char *bytes, size_t count) {
    if (count && l->len + count <= l->buflen) {
        if (l->len == l->pos) {
            memcpy(l->buf+l->pos,bytes,count);
            l->pos += count;
            l->len += count;
            l->buf[l->len] = '\0';
            if (!mlmode &&
                utf8Columns(l->prompt,l->plen) + utf8Columns(l->buf,l->len) < l->cols &&
                !hintsCallback)
            {
                /* Avoid a full update of the line in the
                 * trivial case. */
                if (maskmode == 1) {
                    size_t columns = utf8Columns(bytes,count);
                    while (columns--) {
                        if (write(l->ofd,"*",1) == -1) return -1;
                    }
                } else if (write(l->ofd,bytes,count) == -1) {
                    return -1;
                }
            } else {
                refreshLine(l);
            }
        } else {
            memmove(l->buf+l->pos+count,l->buf+l->pos,l->len-l->pos);
            memcpy(l->buf+l->pos,bytes,count);
            l->len += count;
            l->pos += count;
            l->buf[l->len] = '\0';
            refreshLine(l);
        }
    }
    return 0;
}

/* On error writing to the terminal -1 is returned, otherwise 0. */
int linenoiseEditInsert(struct linenoiseState *l, char c) {
    return linenoiseEditInsertBytes(l,&c,1);
}

/* Move cursor on the left. */
void linenoiseEditMoveLeft(struct linenoiseState *l) {
    if (l->pos > 0) {
        l->pos = utf8PrevChar(l->buf,l->pos);
        refreshLine(l);
    }
}

/* Move cursor on the right. */
void linenoiseEditMoveRight(struct linenoiseState *l) {
    if (l->pos != l->len) {
        l->pos = utf8NextChar(l->buf,l->len,l->pos);
        refreshLine(l);
    }
}

/* Consider letters/digits/underscore as “word”; others as delimiters. */
static int isWordChar(const char *s, size_t pos) {
    unsigned char c = (unsigned char)s[pos];
    if (c >= 0x80) return 1;
    return (c == '_' || (c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
}

static void linenoiseEditMoveWordLeft(struct linenoiseState *l) {
    size_t previous;
    if (l->pos == 0) return;
    /* Skip any delimiters, then move left over the previous word */
    while (l->pos > 0) {
        previous = utf8PrevChar(l->buf,l->pos);
        if (isWordChar(l->buf,previous)) break;
        l->pos = previous;
    }
    /* Then move to the start of that word */
    while (l->pos > 0) {
        previous = utf8PrevChar(l->buf,l->pos);
        if (!isWordChar(l->buf,previous)) break;
        l->pos = previous;
    }
    refreshLine(l);
}

static void linenoiseEditMoveWordRight(struct linenoiseState *l) {
    if (l->pos == l->len) return;
    /* Skip the current word to the right */
    while (l->pos < l->len && isWordChar(l->buf,l->pos))
        l->pos = utf8NextChar(l->buf,l->len,l->pos);
    /* Then skip any delimiters to reach the next word */
    while (l->pos < l->len && !isWordChar(l->buf,l->pos))
        l->pos = utf8NextChar(l->buf,l->len,l->pos);
    refreshLine(l);
}

/* Move cursor to the start of the line. */
void linenoiseEditMoveHome(struct linenoiseState *l) {
    if (l->pos != 0) {
        l->pos = 0;
        refreshLine(l);
    }
}

/* Move cursor to the end of the line. */
void linenoiseEditMoveEnd(struct linenoiseState *l) {
    if (l->pos != l->len) {
        l->pos = l->len;
        refreshLine(l);
    }
}

/* Substitute the currently edited line with the next or previous history
 * entry as specified by 'dir'. */
#define LINENOISE_HISTORY_NEXT 0
#define LINENOISE_HISTORY_PREV 1
void linenoiseEditHistoryNext(struct linenoiseState *l, int dir) {
    if (history_len > 1) {
        /* Update the current history entry before to
         * overwrite it with the next one. */
        free(history[history_len - 1 - l->history_index]);
        history[history_len - 1 - l->history_index] = strdup(l->buf);
        /* Show the new entry */
        l->history_index += (dir == LINENOISE_HISTORY_PREV) ? 1 : -1;
        if (l->history_index < 0) {
            l->history_index = 0;
            return;
        } else if (l->history_index >= history_len) {
            l->history_index = history_len-1;
            return;
        }
        strncpy(l->buf,history[history_len - 1 - l->history_index],l->buflen);
        l->buf[l->buflen-1] = '\0';
        l->len = l->pos = strlen(l->buf);
        refreshLine(l);
    }
}

/* Delete the character at the right of the cursor without altering the cursor
 * position. Basically this is what happens with the "Delete" keyboard key. */
void linenoiseEditDelete(struct linenoiseState *l) {
    if (l->len > 0 && l->pos < l->len) {
        size_t next = utf8NextChar(l->buf,l->len,l->pos);
        memmove(l->buf+l->pos,l->buf+next,l->len-next);
        l->len -= next-l->pos;
        l->buf[l->len] = '\0';
        refreshLine(l);
    }
}

/* Backspace implementation. */
void linenoiseEditBackspace(struct linenoiseState *l) {
    if (l->pos > 0 && l->len > 0) {
        size_t previous = utf8PrevChar(l->buf,l->pos);
        size_t removed = l->pos-previous;
        memmove(l->buf+previous,l->buf+l->pos,l->len-l->pos);
        l->pos = previous;
        l->len -= removed;
        l->buf[l->len] = '\0';
        refreshLine(l);
    }
}

/* Delete the previous word, maintaining the cursor at the start of the
 * current word. */
void linenoiseEditDeletePrevWord(struct linenoiseState *l) {
    size_t old_pos = l->pos;
    size_t diff;
    size_t previous;

    while (l->pos > 0) {
        previous = utf8PrevChar(l->buf,l->pos);
        if (l->buf[previous] != ' ') break;
        l->pos = previous;
    }
    while (l->pos > 0) {
        previous = utf8PrevChar(l->buf,l->pos);
        if (l->buf[previous] == ' ') break;
        l->pos = previous;
    }
    diff = old_pos - l->pos;
    memmove(l->buf+l->pos,l->buf+old_pos,l->len-old_pos+1);
    l->len -= diff;
    refreshLine(l);
}

static void linenoiseEditTranspose(struct linenoiseState *l) {
    size_t previous, next, previous_len, next_len;
    char previous_bytes[4];

    if (l->pos == 0 || l->pos == l->len) return;
    previous = utf8PrevChar(l->buf,l->pos);
    next = utf8NextChar(l->buf,l->len,l->pos);
    previous_len = l->pos-previous;
    next_len = next-l->pos;
    if (previous_len > sizeof(previous_bytes)) return;
    memcpy(previous_bytes,l->buf+previous,previous_len);
    memmove(l->buf+previous,l->buf+l->pos,next_len);
    memcpy(l->buf+previous+next_len,previous_bytes,previous_len);
    l->pos = next < l->len ? next : previous+next_len;
    refreshLine(l);
}

/* This function is the core of the line editing capability of linenoise.
 * It expects 'fd' to be already in "raw mode" so that every key pressed
 * will be returned ASAP to read().
 *
 * The resulting string is put into 'buf' when the user type enter, or
 * when ctrl+d is typed.
 *
 * The function returns the length of the current buffer. */
static int linenoiseEdit(int stdin_fd, int stdout_fd, char *buf, size_t buflen, const char *prompt)
{
    struct linenoiseState l;

    /* Populate the linenoise state that we pass to functions implementing
     * specific editing functionalities. */
    l.ifd = stdin_fd;
    l.ofd = stdout_fd;
    l.buf = buf;
    l.buflen = buflen;
    l.prompt = prompt;
    l.plen = strlen(prompt);
    l.oldpos = l.pos = 0;
    l.len = 0;
    l.cols = getColumns(stdin_fd, stdout_fd);
    l.maxrows = 0;
    l.history_index = 0;

    /* Buffer starts empty. */
    l.buf[0] = '\0';
    l.buflen--; /* Make sure there is always space for the nulterm */

    /* The latest history entry is always our current buffer, that
     * initially is just an empty string. */
    linenoiseHistoryAdd("", 0);

    if (write(l.ofd,prompt,l.plen) == -1) return -1;
    while(1) {
        char input[4];
        char c;
        int nread;
        char seq[3];
#ifdef _WIN32
        int win32_key_action = WIN32_KEY_NONE;
#endif

#ifdef _WIN32
        if (getenv("FAKETTY_WITH_PROMPT") != NULL) {
            nread = (int)read(l.ifd,input,1);
        } else {
            nread = win32read(input,sizeof(input),&win32_key_action);
        }
#else
        nread = read(l.ifd,input,1);
#endif
        if (nread <= 0) return (int)l.len;
#ifdef _WIN32
        if (win32_key_action != WIN32_KEY_NONE) {
            switch (win32_key_action) {
            case WIN32_KEY_DELETE:
                linenoiseEditDelete(&l);
                break;
            case WIN32_KEY_WORD_LEFT:
                linenoiseEditMoveWordLeft(&l);
                break;
            case WIN32_KEY_WORD_RIGHT:
                linenoiseEditMoveWordRight(&l);
                break;
            }
            continue;
        }
#endif
        c = input[0];

        /* Only autocomplete when the callback is set. It returns < 0 when
         * there was an error reading from fd. Otherwise it will return the
         * character that should be handled next. */
        if (nread == 1 && c == TAB && completionCallback != NULL && !reverse_search_mode_enabled) {
#ifdef _WIN32
            nread = completeLine(&l,input,sizeof(input),&win32_key_action);
#else
            nread = completeLine(&l,input,sizeof(input),NULL);
#endif
            /* Return on errors */
            if (nread < 0) return (int)l.len;
            /* Read next character when 0 */
            if (nread == 0) continue;
#ifdef _WIN32
            if (win32_key_action != WIN32_KEY_NONE) {
                switch (win32_key_action) {
                case WIN32_KEY_DELETE:
                    linenoiseEditDelete(&l);
                    break;
                case WIN32_KEY_WORD_LEFT:
                    linenoiseEditMoveWordLeft(&l);
                    break;
                case WIN32_KEY_WORD_RIGHT:
                    linenoiseEditMoveWordRight(&l);
                    break;
                }
                continue;
            }
#endif
            c = input[0];
        }

        if (nread > 1) {
            if (linenoiseEditInsertBytes(&l,input,(size_t)nread)) return -1;
            continue;
        }

        switch(c) {
        case NL:       /* enter, typed before raw mode was enabled */
            break;
        case TAB:
            if (reverse_search_mode_enabled) disableReverseSearchMode(&l, buf, buflen, 0);
            break;
        case ENTER:    /* enter */
            history_len--;
            free(history[history_len]);
            if (mlmode) linenoiseEditMoveEnd(&l);
            if (hintsCallback) {
                /* Force a refresh without hints to leave the previous
                 * line as the user typed it after a newline. */
                linenoiseHintsCallback *hc = hintsCallback;
                hintsCallback = NULL;
                refreshLine(&l);
                hintsCallback = hc;
            }

            if (reverse_search_mode_enabled) disableReverseSearchMode(&l, buf, buflen, 0);
            return (int)l.len;
        case CTRL_C:     /* ctrl-c */
            if (reverse_search_mode_enabled) {
                disableReverseSearchMode(&l, buf, buflen, 1);
                break;
            }
            errno = EAGAIN;
            return -1;
        case BACKSPACE:   /* backspace */
        case 8:     /* ctrl-h */
            linenoiseEditBackspace(&l);
            break;
        case CTRL_D:     /* ctrl-d, remove char at right of cursor, or if the
                            line is empty, act as end-of-file. */
            if (l.len > 0) {
                linenoiseEditDelete(&l);
            } else {
                history_len--;
                free(history[history_len]);
                return -1;
            }
            break;
        case CTRL_T:    /* ctrl-t, swaps current character with previous. */
            linenoiseEditTranspose(&l);
            break;
        case CTRL_B:     /* ctrl-b */
            linenoiseEditMoveLeft(&l);
            break;
        case CTRL_F:     /* ctrl-f */
            linenoiseEditMoveRight(&l);
            break;
        case CTRL_P:    /* ctrl-p */
            linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_PREV);
            break;
        case CTRL_R:
        case CTRL_S:
            reverse_search_direction = c == CTRL_R ? -1 : 1;
            if (reverse_search_mode_enabled) {
                /* cycle search results */
                cycle_to_next_search = 1;
                l.prompt = REVERSE_SEARCH_PROMPT(reverse_search_direction);
                refreshLine(&l);
                break;
            }
            buf[0] = '\0';
            l.pos = l.len = 0;
            enableReverseSearchMode(&l);
            break;
        case CTRL_G:
            if (reverse_search_mode_enabled) disableReverseSearchMode(&l, buf, buflen, 1);
            break;
        case CTRL_N:    /* ctrl-n */
            linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_NEXT);
            break;
        case ESC:    /* escape sequence */
            /* Read the next two bytes representing the escape sequence.
             * Use two calls to handle slow terminals returning the two
             * chars at different times. */
            if (read(l.ifd,seq,1) == -1) break;

            /* Handle Meta-b / Meta-f directly because it's a 2-byte sequence */
            if (seq[0] == 'b' || seq[0] == 'f') {
                if (reverse_search_mode_enabled) {
                    disableReverseSearchMode(&l, buf, buflen, 1);
                    break;
                }
                if (seq[0] == 'b') linenoiseEditMoveWordLeft(&l);   /* ESC b → word left */
                else linenoiseEditMoveWordRight(&l);                /* ESC f → word right */
                break;
            }

            if (read(l.ifd,seq+1,1) == -1) break;

            if (reverse_search_mode_enabled) {
                disableReverseSearchMode(&l, buf, buflen, 1);
                break;
            }

            /* ESC [ sequences. */
            if (seq[0] == '[') {
                if (seq[1] >= '0' && seq[1] <= '9') {
                    /* Extended escape, read additional bytes.
                     * Examples: ESC [1;5C  ESC [3~ */
                    char seq_buffer[SEQ_BUFFER_MAX_LENGTH];
                    int i = 0;
                    seq_buffer[i++] = seq[1];

                    /* Read more bytes until we see a CSI final byte (range @..~).
                     * Use SEQ_BUFFER_MAX_LENGTH-1 to reserve one position for '\0'. */
                    char seq_char;
                    while (i < SEQ_BUFFER_MAX_LENGTH - 1 && read(l.ifd, &seq_char, 1) == 1) {
                        seq_buffer[i++] = seq_char;
                        if (seq_char >= '@' && seq_char <= '~') break; /* CSI final byte */
                    }
                    seq_buffer[i] = '\0';

                    /* The exact key mapping behavior depends on your keyboard/terminal setup.
                     * For example, in MacOS terminal you can go to the profile keyboard setting
                     * to see or configure the current mapping.
                     *
                     * Take action `[1;5C` (Ctrl + →) or `[1;3D` (Alt + ←) as examples:
                     * [ indicates a CSI (Control Sequence Introducer), telling the terminal
                     *    "What follows is a control command, not text"
                     * 1 is how many units to move (default is 1 if omitted)
                     * ; is the separator between parameters
                     * 5 is the modifier mask for Ctrl. Other possible values include 1 (no modifier),
                     *     2 (Shift), 3 (Alt), 4 (Shift + Alt), 6 (Shift + Ctrl), 7 (Alt + Ctrl), etc.
                     * C is the cursor right command. Other commands include A (cursor up), B (cursor
                     *     down), D (cursor left).
                     */

                    /* Word left: Ctrl + ← (modifier 5) or Alt + ← (modifier 3) */
                    if (strcmp(seq_buffer, "1;5D") == 0 || strcmp(seq_buffer, "1;3D") == 0) {
                        linenoiseEditMoveWordLeft(&l);
                        break;
                    }
                    /* Word right: Ctrl + → (modifier 5) or Alt + → (modifier 3) */
                    if (strcmp(seq_buffer, "1;5C") == 0 || strcmp(seq_buffer, "1;3C") == 0) {
                        linenoiseEditMoveWordRight(&l);
                        break;
                    }
                    /* Delete key */
                    if (strcmp(seq_buffer, "3~") == 0) {
                        linenoiseEditDelete(&l);
                        break;
                    }
                } else {
                    switch(seq[1]) {
                    case 'A': /* Up */
                        linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_PREV);
                        break;
                    case 'B': /* Down */
                        linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_NEXT);
                        break;
                    case 'C': /* Right */
                        linenoiseEditMoveRight(&l);
                        break;
                    case 'D': /* Left */
                        linenoiseEditMoveLeft(&l);
                        break;
                    case 'H': /* Home */
                        linenoiseEditMoveHome(&l);
                        break;
                    case 'F': /* End*/
                        linenoiseEditMoveEnd(&l);
                        break;
                    }
                }
            }

            /* ESC O sequences. */
            else if (seq[0] == 'O') {
                switch(seq[1]) {
                case 'H': /* Home */
                    linenoiseEditMoveHome(&l);
                    break;
                case 'F': /* End*/
                    linenoiseEditMoveEnd(&l);
                    break;
                }
            }
            break;
        default:
            if (linenoiseEditInsert(&l,c)) return -1;
            break;
        case CTRL_U: /* Ctrl+u, delete the whole line. */
            buf[0] = '\0';
            l.pos = l.len = 0;
            refreshLine(&l);
            break;
        case CTRL_K: /* Ctrl+k, delete from current to end of line. */
            buf[l.pos] = '\0';
            l.len = l.pos;
            refreshLine(&l);
            break;
        case CTRL_A: /* Ctrl+a, go to the start of the line */
            linenoiseEditMoveHome(&l);
            break;
        case CTRL_E: /* ctrl+e, go to the end of the line */
            linenoiseEditMoveEnd(&l);
            break;
        case CTRL_L: /* ctrl+l, clear screen */
            linenoiseClearScreen();
            refreshLine(&l);
            break;
        case CTRL_W: /* ctrl+w, delete previous word */
            linenoiseEditDeletePrevWord(&l);
            break;
        }
    }
    return (int)l.len;
}

/* This special mode is used by linenoise in order to print scan codes
 * on screen for debugging / development purposes. It is implemented
 * by the linenoise_example program using the --keycodes option. */
void linenoisePrintKeyCodes(void) {
    char quit[4];

    printf("Linenoise key codes debugging mode.\n"
            "Press keys to see scan codes. Type 'quit' at any time to exit.\n");
    if (enableRawMode(STDIN_FILENO) == -1) return;
    memset(quit,' ',4);
    while(1) {
        char c;
        int nread;

        nread = (int)read(STDIN_FILENO,&c,1);
        if (nread <= 0) continue;
        memmove(quit,quit+1,sizeof(quit)-1); /* shift string to left. */
        quit[sizeof(quit)-1] = c; /* Insert current char on the right. */
        if (memcmp(quit,"quit",sizeof(quit)) == 0) break;

        printf("'%c' %02x (%d) (type quit to exit)\n",
            isprint(c) ? c : '?', (int)c, (int)c);
        printf("\r"); /* Go left edge manually, we are in raw mode. */
        fflush(stdout);
    }
    disableRawMode(STDIN_FILENO);
}

/* This function calls the line editing function linenoiseEdit() using
 * the STDIN file descriptor set in raw mode. */
static int linenoiseRaw(char *buf, size_t buflen, const char *prompt) {
    int count;

    if (buflen == 0) {
        errno = EINVAL;
        return -1;
    }

    if (enableRawMode(STDIN_FILENO) == -1) return -1;
    count = linenoiseEdit(STDIN_FILENO, STDOUT_FILENO, buf, buflen, prompt);
    disableRawMode(STDIN_FILENO);
    printf("\n");
    return count;
}

/* This function is called when linenoise() is called with the standard
 * input file descriptor not attached to a TTY. So for example when the
 * program using linenoise is called in pipe or with a file redirected
 * to its standard input. In this case, we want to be able to return the
 * line regardless of its length (by default we are limited to 4k). */
static char *linenoiseNoTTY(void) {
    char *line = NULL;
    size_t len = 0, maxlen = 0;

    while(1) {
        if (len == maxlen) {
            if (maxlen == 0) maxlen = 16;
            maxlen *= 2;
            char *oldval = line;
            line = realloc(line,maxlen);
            if (line == NULL) {
                if (oldval) free(oldval);
                return NULL;
            }
        }
        int c = fgetc(stdin);
        if (c == EOF || c == '\n') {
            if (c == EOF && len == 0) {
                free(line);
                return NULL;
            } else {
                line[len] = '\0';
                return line;
            }
        } else {
            line[len] = c;
            len++;
        }
    }
}

/* The high level function that is the main API of the linenoise library.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses dummy fgets() so that you will be able to type
 * something even in the most desperate of the conditions. */
char *linenoise(const char *prompt) {
    char buf[LINENOISE_MAX_LINE] = {0};
    int count;

    if (getenv("FAKETTY_WITH_PROMPT") == NULL && !isatty(STDIN_FILENO)) {
        /* Not a tty: read from file / pipe. In this mode we don't want any
         * limit to the line size, so we call a function to handle that. */
        return linenoiseNoTTY();
    } else if (getenv("FAKETTY_WITH_PROMPT") == NULL && isUnsupportedTerm()) {
        size_t len;

        printf("%s",prompt);
        fflush(stdout);
        if (fgets(buf,LINENOISE_MAX_LINE,stdin) == NULL) return NULL;
        len = strlen(buf);
        while(len && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            len--;
            buf[len] = '\0';
        }
        return strdup(buf);
    } else {
        count = linenoiseRaw(buf,LINENOISE_MAX_LINE,prompt);
        if (count == -1) return NULL;
        return strdup(buf);
    }
}

/* This is just a wrapper the user may want to call in order to make sure
 * the linenoise returned buffer is freed with the same allocator it was
 * created with. Useful when the main program is using an alternative
 * allocator. */
void linenoiseFree(void *ptr) {
    free(ptr);
}

/* ================================ History ================================= */

/* Free the history, but does not reset it. Only used when we have to
 * exit() to avoid memory leaks are reported by valgrind & co. */
static void freeHistory(void) {
    if (history) {
        int j;

        for (j = 0; j < history_len; j++)
            free(history[j]);
        free(history);
        free(history_sensitive);
    }
}

/* At exit we'll try to fix the terminal to the initial conditions. */
static void linenoiseAtExit(void) {
    disableRawMode(STDIN_FILENO);
    freeHistory();
}

/* This is the API call to add a new entry in the linenoise history.
 * It uses a fixed array of char pointers that are shifted (memmoved)
 * when the history max length is reached in order to remove the older
 * entry and make room for the new one, so it is not exactly suitable for huge
 * histories, but will work well for a few hundred of entries.
 *
 * Using a circular buffer is smarter, but a bit more complex to handle. */
int linenoiseHistoryAdd(const char *line, int is_sensitive) {
    char *linecopy;

    if (history_max_len == 0) return 0;

    /* Initialization on first call. */
    if (history == NULL) {
        history = malloc(sizeof(char*)*history_max_len);
        if (history == NULL) return 0;
        history_sensitive = malloc(sizeof(int)*history_max_len);
        if (history_sensitive == NULL) {
            free(history);
            history = NULL;
            return 0;
        }
        memset(history,0,(sizeof(char*)*history_max_len));
        memset(history_sensitive,0,(sizeof(int)*history_max_len));
    }

    /* Don't add duplicated lines. */
    if (history_len && !strcmp(history[history_len-1], line)) return 0;

    /* Add an heap allocated copy of the line in the history.
     * If we reached the max length, remove the older line. */
    linecopy = strdup(line);
    if (!linecopy) return 0;
    if (history_len == history_max_len) {
        free(history[0]);
        memmove(history,history+1,sizeof(char*)*(history_max_len-1));
        memmove(history_sensitive,history_sensitive+1,sizeof(int)*(history_max_len-1));
        history_len--;
    }
    history[history_len] = linecopy;
    history_sensitive[history_len] = is_sensitive;
    history_len++;
    return 1;
}

/* Set the maximum length for the history. This function can be called even
 * if there is already some history, the function will make sure to retain
 * just the latest 'len' elements if the new history length value is smaller
 * than the amount of items already inside the history. */
int linenoiseHistorySetMaxLen(int len) {
    char **new;
    int *new_sensitive;

    if (len < 1) return 0;
    if (history) {
        int tocopy = history_len;

        new = malloc(sizeof(char*)*len);
        if (new == NULL) return 0;
        new_sensitive = malloc(sizeof(int)*len);
        if (new_sensitive == NULL) {
            free(new);
            return 0;
        }

        /* If we can't copy everything, free the elements we'll not use. */
        if (len < tocopy) {
            int j;

            for (j = 0; j < tocopy-len; j++) free(history[j]);
            tocopy = len;
        }
        memset(new,0,sizeof(char*)*len);
        memset(new_sensitive,0,sizeof(int)*len);
        memcpy(new,history+(history_len-tocopy), sizeof(char*)*tocopy);
        memcpy(new_sensitive,history_sensitive+(history_len-tocopy), sizeof(int)*tocopy);
        free(history);
        free(history_sensitive);
        history = new;
        history_sensitive = new_sensitive;
    }
    history_max_len = len;
    if (history_len > history_max_len)
        history_len = history_max_len;
    return 1;
}

/* Save the history in the specified file. On success 0 is returned
 * otherwise -1 is returned. */
int linenoiseHistorySave(const char *filename) {
#ifndef _WIN32
    mode_t old_umask = umask(S_IXUSR|S_IRWXG|S_IRWXO);
#endif
    FILE *fp;
    int j;
    int result = 0;

#ifdef _WIN32
    fp = replace_fopen(filename,"w");
#else
    fp = fopen(filename,"w");
    umask(old_umask);
#endif
    if (fp == NULL) return -1;
#ifdef _WIN32
    if (replace_chmod(filename,S_IRUSR|S_IWUSR) != 0) {
#else
    if (chmod(filename,S_IRUSR|S_IWUSR) != 0) {
#endif
        int saved_errno = errno;
        fclose(fp);
        errno = saved_errno;
        return -1;
    }
    for (j = 0; j < history_len; j++) {
        if (!history_sensitive[j] && fprintf(fp,"%s\n",history[j]) < 0) {
            result = -1;
            break;
        }
    }
    if (fclose(fp) != 0) result = -1;
    return result;
}

/* Load the history from the specified file. If the file does not exist
 * zero is returned and no operation is performed.
 *
 * If the file exists and the operation succeeded 0 is returned, otherwise
 * on error -1 is returned. */
int linenoiseHistoryLoad(const char *filename) {
#ifdef _WIN32
    FILE *fp = replace_fopen(filename,"r");
#else
    FILE *fp = fopen(filename,"r");
#endif
    char buf[LINENOISE_MAX_LINE];
    int result = 0;

    if (fp == NULL) return -1;

    while (fgets(buf,LINENOISE_MAX_LINE,fp) != NULL) {
        char *p;

        p = strchr(buf,'\r');
        if (!p) p = strchr(buf,'\n');
        if (p) *p = '\0';
        linenoiseHistoryAdd(buf, 0);
    }
    if (ferror(fp)) result = -1;
    if (fclose(fp) != 0) result = -1;
    return result;
}

/* This function updates the search index based on the direction of the search.
 * Returns 0 if the beginning or end of the history is reached, otherwise, returns 1. */
static int setNextSearchIndex(int *i) {
    if (reverse_search_direction == 1) {
        if (*i == history_len-1) return 0;
        *i = *i + 1;
    } else {
        if (*i <= 0) return 0;
        *i = *i - 1;
    }
    return 1;
}

linenoiseHistorySearchResult searchInHistory(char *search_term) {
    linenoiseHistorySearchResult result = {0};

    if (!history_len || !strlen(search_term)) return result;

    int i = cycle_to_next_search ? search_result_history_index :
        (reverse_search_direction == -1 ? history_len-1 : 0);

    while (1) {
        char *found = strstr(history[i], search_term);

        /* check if we found the same string at another index when cycling, this would be annoying to cycle through
         * as it might appear that cycling isn't working */
        int strings_are_the_same = cycle_to_next_search && strcmp(history[i], history[search_result_history_index]) == 0;

        if (found && !strings_are_the_same) {
            int haystack_index = found - history[i];
            result.result = history[i];
            result.len = strlen(history[i]);
            result.search_term_index = haystack_index;
            result.search_term_len = strlen(search_term);
            search_result_history_index = i;
            break;
        }

        /* Exit if reached the end. */
        if (!setNextSearchIndex(&i)) break;
    }

    return result;
}

static void refreshSearchResult(struct linenoiseState *ls) {
   if (!reverse_search_mode_enabled) {
        return;
    }

    linenoiseHistorySearchResult sr = searchInHistory(ls->buf);
    int found = sr.result && sr.len;

    /* If the search term has not changed and we are cycling to the next search result
     * (using CTRL+R or CTRL+S), there is no need to reset the old search result. */
    if (!cycle_to_next_search || found)
        resetSearchResult();
    cycle_to_next_search = 0;

    if (found) {
        char *bold = "\x1B[1m";
        char *normal = "\x1B[0m";

        size_t size_needed = sr.search_term_index + sr.search_term_len + sr.len -
            (sr.search_term_index+sr.search_term_len) + sizeof(normal) + sizeof(bold) + sizeof(normal);
        if (size_needed > sizeof(search_result_friendly) - 1) {
            return;
        }

        /* Allocate memory for the prefix, match, and suffix strings, one extra byte for `\0`. */
        char *prefix = calloc(sr.search_term_index + 1, sizeof(char));
        char *match = calloc(sr.search_term_len + 1, sizeof(char));
        char *suffix = calloc(sr.len - (sr.search_term_index+sr.search_term_len) + 1, sizeof(char));

        memcpy(prefix, sr.result, sr.search_term_index);
        memcpy(match, sr.result + sr.search_term_index, sr.search_term_len);
        memcpy(suffix, sr.result + sr.search_term_index + sr.search_term_len,
               sr.len - (sr.search_term_index+sr.search_term_len));
        sprintf(search_result, "%s%s%s", prefix, match, suffix);
        sprintf(search_result_friendly, "%s%s%s%s%s%s", normal, prefix, bold, match, normal, suffix);

        free(prefix);
        free(match);
        free(suffix);

        search_result_start_offset = sr.search_term_index;
    }
}
