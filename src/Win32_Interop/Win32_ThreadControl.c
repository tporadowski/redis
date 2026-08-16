/*
 * Copyright (c), Microsoft Open Technologies, Inc.
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "Win32_ThreadControl.h"
#include <windows.h>

static volatile LONG g_NumWorkerThreads;
static volatile LONG g_NumWorkerThreadsInSafeMode;
static volatile LONG g_SuspensionRequested;
static HANDLE g_hResumeFromSuspension;
static CRITICAL_SECTION g_ThreadControlMutex;
static volatile LONG g_ready;

void InitThreadControl(void) {
    if (InterlockedCompareExchange(&g_ready, 1, 0) != 0)
        return;
    InitializeCriticalSection(&g_ThreadControlMutex);
    g_hResumeFromSuspension = CreateEvent(NULL, TRUE, TRUE, NULL);
    if (!g_hResumeFromSuspension)
        g_ready = 0;
}

static void ensure_thread_control(void) {
    if (!g_ready)
        InitThreadControl();
}

void IncrementWorkerThreadCount(void) {
    ensure_thread_control();
    EnterCriticalSection(&g_ThreadControlMutex);
    g_NumWorkerThreads++;
    LeaveCriticalSection(&g_ThreadControlMutex);
}

void DecrementWorkerThreadCount(void) {
    ensure_thread_control();
    EnterCriticalSection(&g_ThreadControlMutex);
    if (g_NumWorkerThreads > 0)
        g_NumWorkerThreads--;
    LeaveCriticalSection(&g_ThreadControlMutex);
}

int SuspensionCompleted(void) {
    int result;
    ensure_thread_control();
    EnterCriticalSection(&g_ThreadControlMutex);
    result = (g_NumWorkerThreadsInSafeMode == g_NumWorkerThreads);
    LeaveCriticalSection(&g_ThreadControlMutex);
    return result;
}

void RequestSuspension(void) {
    ensure_thread_control();
    if (!g_SuspensionRequested) {
        ResetEvent(g_hResumeFromSuspension);
        InterlockedOr(&g_SuspensionRequested, 1);
    }
}

void ResumeFromSuspension(void) {
    ensure_thread_control();
    InterlockedAnd(&g_SuspensionRequested, 0);
    SetEvent(g_hResumeFromSuspension);
}

void WorkerThread_EnterSafeMode(void) {
    ensure_thread_control();
    EnterCriticalSection(&g_ThreadControlMutex);
    g_NumWorkerThreadsInSafeMode++;
    LeaveCriticalSection(&g_ThreadControlMutex);
}

void WorkerThread_ExitSafeMode(void) {
    ensure_thread_control();
    for (;;) {
        EnterCriticalSection(&g_ThreadControlMutex);
        if (g_SuspensionRequested) {
            LeaveCriticalSection(&g_ThreadControlMutex);
            WaitForSingleObject(g_hResumeFromSuspension, INFINITE);
            continue;
        }
        if (g_NumWorkerThreadsInSafeMode > 0)
            g_NumWorkerThreadsInSafeMode--;
        LeaveCriticalSection(&g_ThreadControlMutex);
        break;
    }
}
