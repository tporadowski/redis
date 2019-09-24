/*
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifdef _WIN32
#include "Win32_Interop/Win32_Portability.h"
#include "Win32_Interop/win32_types.h"
#include "Win32_Interop/Win32_Error.h"
#endif
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <errno.h>
#ifndef _WIN32
#include <termios.h>
#include <sys/ioctl.h>
#if defined(__sun)
#include <stropts.h>
#endif
#else
#include "Win32_Interop/win32fixes.h"
#include "Win32_Interop/win32_ANSI.h"
#endif
#include "config.h"


#if (PORT_ULONG_MAX == 4294967295UL)
#define MEMTEST_32BIT
#elif (PORT_ULONG_MAX == 18446744073709551615ULL)
#define MEMTEST_64BIT
#else
#error "PORT_ULONG_MAX value not supported."
#endif

#ifdef MEMTEST_32BIT
#define ULONG_ONEZERO 0xaaaaaaaaUL
#define ULONG_ZEROONE 0x55555555UL
#else
#define ULONG_ONEZERO 0xaaaaaaaaaaaaaaaaUL
#define ULONG_ZEROONE 0x5555555555555555UL
#endif

#ifdef _WIN32
typedef struct winsize
{
    unsigned short ws_row;
    unsigned short ws_col;
};
#endif

static struct winsize ws;
size_t progress_printed; /* Printed chars in screen-wide progress bar. */
size_t progress_full; /* How many chars to write to fill the progress bar. */

void memtest_progress_start(char *title, int pass) {
    int j;

    printf("\x1b[H\x1b[2J");    /* Cursor home, clear screen. */
    /* Fill with dots. */
    for (j = 0; j < ws.ws_col*(ws.ws_row-2); j++) printf(".");
    printf("Please keep the test running several minutes per GB of memory.\n");
    printf("Also check http://www.memtest86.com/ and http://pyropus.ca/software/memtester/");
    printf("\x1b[H\x1b[2K");          /* Cursor home, clear current line.  */
    printf("%s [%d]\n", title, pass); /* Print title. */
    progress_printed = 0;
    progress_full = ws.ws_col*(ws.ws_row-3);
    fflush(stdout);
}

void memtest_progress_end(void) {
    printf("\x1b[H\x1b[2J");    /* Cursor home, clear screen. */
}

void memtest_progress_step(size_t curr, size_t size, char c) {
    size_t chars = ((PORT_ULONGLONG)curr*progress_full)/size, j;

    for (j = 0; j < chars-progress_printed; j++) printf("%c",c);
    progress_printed = chars;
    fflush(stdout);
}

/* Test that addressing is fine. Every location is populated with its own
 * address, and finally verified. This test is very fast but may detect
 * ASAP big issues with the memory subsystem. */
int memtest_addressing(PORT_ULONG *l, size_t bytes, int interactive) {
    PORT_ULONG words = (PORT_ULONG)(bytes/sizeof(PORT_ULONG));
    PORT_ULONG j, *p;

    /* Fill */
    p = l;
    for (j = 0; j < words; j++) {
        *p = (PORT_ULONG)p;
        p++;
        if ((j & 0xffff) == 0 && interactive)
            memtest_progress_step(j,words*2,'A');
    }
    /* Test */
    p = l;
    for (j = 0; j < words; j++) {
        if (*p != (PORT_ULONG)p) {
            if (interactive) {
                printf("\n*** MEMORY ADDRESSING ERROR: %p contains %Iu\n",      WIN_PORT_FIX /* %lu -> %Iu */
                    (void*) p, *p);
                exit(1);
            }
            return 1;
        }
        p++;
        if ((j & 0xffff) == 0 && interactive)
            memtest_progress_step(j+words,words*2,'A');
    }
    return 0;
}

/* Fill words stepping a single page at every write, so we continue to
 * touch all the pages in the smallest amount of time reducing the
 * effectiveness of caches, and making it hard for the OS to transfer
 * pages on the swap.
 *
 * In this test we can't call rand() since the system may be completely
 * unable to handle library calls, so we have to resort to our own
 * PRNG that only uses local state. We use an xorshift* PRNG. */
#define xorshift64star_next() do { \
        rseed ^= rseed >> 12; \
        rseed ^= rseed << 25; \
        rseed ^= rseed >> 27; \
        rout = rseed * UINT64_C(2685821657736338717); \
} while(0)

void memtest_fill_random(PORT_ULONG *l, size_t bytes, int interactive) {
    PORT_ULONG step = (PORT_ULONG)(4096/sizeof(PORT_ULONG));
    PORT_ULONG words = (PORT_ULONG)(bytes/sizeof(PORT_ULONG)/2);
    PORT_ULONG iwords = words/step;  /* words per iteration */
    PORT_ULONG off, w, *l1, *l2;
    uint64_t rseed = UINT64_C(0xd13133de9afdb566); /* Just a random seed. */
    uint64_t rout = 0;

    assert((bytes & 4095) == 0);
    for (off = 0; off < step; off++) {
        l1 = l+off;
        l2 = l1+words;
        for (w = 0; w < iwords; w++) {
            xorshift64star_next();
            *l1 = *l2 = (PORT_ULONG) rout;
            l1 += step;
            l2 += step;
            if ((w & 0xffff) == 0 && interactive)
                memtest_progress_step(w+iwords*off,words,'R');
        }
    }
}

/* Like memtest_fill_random() but uses the two specified values to fill
 * memory, in an alternated way (v1|v2|v1|v2|...) */
void memtest_fill_value(PORT_ULONG *l, size_t bytes, PORT_ULONG v1,
                        PORT_ULONG v2, char sym, int interactive)
{
    PORT_ULONG step = (PORT_ULONG)(4096/sizeof(PORT_ULONG));
    PORT_ULONG words = (PORT_ULONG)(bytes/sizeof(PORT_ULONG)/2);
    PORT_ULONG iwords = words/step;  /* words per iteration */
    PORT_ULONG off, w, *l1, *l2, v;

    assert((bytes & 4095) == 0);
    for (off = 0; off < step; off++) {
        l1 = l+off;
        l2 = l1+words;
        v = (off & 1) ? v2 : v1;
        for (w = 0; w < iwords; w++) {
#ifdef MEMTEST_32BIT
            *l1 = *l2 = ((PORT_ULONG)     v) |
                        (((PORT_ULONG)    v) << 16);
#else
            *l1 = *l2 = ((PORT_ULONG)     v) |
                        (((PORT_ULONG)    v) << 16) |
                        (((PORT_ULONG)    v) << 32) |
                        (((PORT_ULONG)    v) << 48);
#endif
            l1 += step;
            l2 += step;
            if ((w & 0xffff) == 0 && interactive)
                memtest_progress_step(w+iwords*off,words,sym);
        }
    }
}

int memtest_compare(PORT_ULONG *l, size_t bytes, int interactive) {
    PORT_ULONG words = (PORT_ULONG)(bytes/sizeof(PORT_ULONG)/2);
    PORT_ULONG w, *l1, *l2;

    assert((bytes & 4095) == 0);
    l1 = l;
    l2 = l1+words;
    for (w = 0; w < words; w++) {
        if (*l1 != *l2) {
            if (interactive) {
                printf("\n*** MEMORY ERROR DETECTED: %p != %p (%Iu vs %Iu)\n",  WIN_PORT_FIX /* %lu -> %Iu */
                    (void*)l1, (void*)l2, *l1, *l2);
                exit(1);
            }
            return 1;
        }
        l1 ++;
        l2 ++;
        if ((w & 0xffff) == 0 && interactive)
            memtest_progress_step(w,words,'=');
    }
    return 0;
}

int memtest_compare_times(PORT_ULONG *m, size_t bytes, int pass, int times,
                          int interactive)
{
    int j;
    int errors = 0;

    for (j = 0; j < times; j++) {
        if (interactive) memtest_progress_start("Compare",pass);
        errors += memtest_compare(m,bytes,interactive);
        if (interactive) memtest_progress_end();
    }
    return errors;
}

/* Test the specified memory. The number of bytes must be multiple of 4096.
 * If interactive is true the program exists with an error and prints
 * ASCII arts to show progresses. Instead when interactive is 0, it can
 * be used as an API call, and returns 1 if memory errors were found or
 * 0 if there were no errors detected. */
int memtest_test(PORT_ULONG *m, size_t bytes, int passes, int interactive) {
    int pass = 0;
    int errors = 0;

    while (pass != passes) {
        pass++;

        if (interactive) memtest_progress_start("Addressing test",pass);
        errors += memtest_addressing(m,bytes,interactive);
        if (interactive) memtest_progress_end();

        if (interactive) memtest_progress_start("Random fill",pass);
        memtest_fill_random(m,bytes,interactive);
        if (interactive) memtest_progress_end();
        errors += memtest_compare_times(m,bytes,pass,4,interactive);

        if (interactive) memtest_progress_start("Solid fill",pass);
        memtest_fill_value(m,bytes,0,(PORT_ULONG)-1,'S',interactive);
        if (interactive) memtest_progress_end();
        errors += memtest_compare_times(m,bytes,pass,4,interactive);

        if (interactive) memtest_progress_start("Checkerboard fill",pass);
        memtest_fill_value(m,bytes,ULONG_ONEZERO,ULONG_ZEROONE,'C',interactive);
        if (interactive) memtest_progress_end();
        errors += memtest_compare_times(m,bytes,pass,4,interactive);
    }
    return errors;
}

/* A version of memtest_test() that tests memory in small pieces
 * in order to restore the memory content at exit.
 *
 * One problem we have with this approach, is that the cache can avoid
 * real memory accesses, and we can't test big chunks of memory at the
 * same time, because we need to backup them on the stack (the allocator
 * may not be usable or we may be already in an out of memory condition).
 * So what we do is to try to trash the cache with useless memory accesses
 * between the fill and compare cycles. */
#define MEMTEST_BACKUP_WORDS (1024*(1024/sizeof(PORT_LONG)))
/* Random accesses of MEMTEST_DECACHE_SIZE are performed at the start and
 * end of the region between fill and compare cycles in order to trash
 * the cache. */
#define MEMTEST_DECACHE_SIZE (1024*8)
int memtest_preserving_test(PORT_ULONG *m, size_t bytes, int passes) {
    PORT_ULONG backup[MEMTEST_BACKUP_WORDS];
    PORT_ULONG *p = m;
    PORT_ULONG *end = (PORT_ULONG*) (((unsigned char*)m)+(bytes-MEMTEST_DECACHE_SIZE));
    size_t left = bytes;
    int errors = 0;

    if (bytes & 4095) return 0; /* Can't test across 4k page boundaries. */
    if (bytes < 4096*2) return 0; /* Can't test a single page. */

    while(left) {
        /* If we have to test a single final page, go back a single page
         * so that we can test two pages, since the code can't test a single
         * page but at least two. */
        if (left == 4096) {
            left += 4096;
            p -= 4096/sizeof(PORT_ULONG);
        }

        int pass = 0;
        size_t len = (left > sizeof(backup)) ? sizeof(backup) : left;

        /* Always test an even number of pages. */
        if (len/4096 % 2) len -= 4096;

        memcpy(backup,p,len); /* Backup. */
        while(pass != passes) {
            pass++;
            errors += memtest_addressing(p,len,0);
            memtest_fill_random(p,len,0);
            if (bytes >= MEMTEST_DECACHE_SIZE) {
                memtest_compare_times(m,MEMTEST_DECACHE_SIZE,pass,1,0);
                memtest_compare_times(end,MEMTEST_DECACHE_SIZE,pass,1,0);
            }
            errors += memtest_compare_times(p,len,pass,4,0);
            memtest_fill_value(p,len,0,(PORT_ULONG)-1,'S',0);
            if (bytes >= MEMTEST_DECACHE_SIZE) {
                memtest_compare_times(m,MEMTEST_DECACHE_SIZE,pass,1,0);
                memtest_compare_times(end,MEMTEST_DECACHE_SIZE,pass,1,0);
            }
            errors += memtest_compare_times(p,len,pass,4,0);
            memtest_fill_value(p,len,ULONG_ONEZERO,ULONG_ZEROONE,'C',0);
            if (bytes >= MEMTEST_DECACHE_SIZE) {
                memtest_compare_times(m,MEMTEST_DECACHE_SIZE,pass,1,0);
                memtest_compare_times(end,MEMTEST_DECACHE_SIZE,pass,1,0);
            }
            errors += memtest_compare_times(p,len,pass,4,0);
        }
        memcpy(p,backup,len); /* Restore. */
        left -= len;
        p += len/sizeof(PORT_ULONG);
    }
    return errors;
}

/* Perform an interactive test allocating the specified number of megabytes. */
void memtest_alloc_and_test(size_t megabytes, int passes) {
    size_t bytes = megabytes*1024*1024;
    PORT_ULONG *m = malloc(bytes);

    if (m == NULL) {
        fprintf(stderr,"Unable to allocate %Iu megabytes: %s",                 WIN_PORT_FIX /* %zu -> %Iu */
            megabytes, IF_WIN32(wsa_strerror(errno), strerror(errno)));
        exit(1);
    }
    memtest_test(m,bytes,passes,1);
    free(m);
}

void memtest(size_t megabytes, int passes) {
#ifdef _WIN32
    HANDLE hOut;
    CONSOLE_SCREEN_BUFFER_INFO b;

    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleScreenBufferInfo(hOut, &b)) {
        ws.ws_col = b.dwSize.X;
        ws.ws_row = b.dwSize.Y;
    } else {
        ws.ws_col = 80;
        ws.ws_row = 20;
    }
#else
    if (ioctl(1, TIOCGWINSZ, &ws) == -1) {
        ws.ws_col = 80;
        ws.ws_row = 20;
    }
#endif
    memtest_alloc_and_test(megabytes,passes);
    printf("\nYour memory passed this test.\n");
    printf("Please if you are still in doubt use the following two tools:\n");
    printf("1) memtest86: http://www.memtest86.com/\n");
    printf("2) memtester: http://pyropus.ca/software/memtester/\n");
    exit(0);
}
