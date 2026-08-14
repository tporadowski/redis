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

#include "win32_rfdmap.h"
#include <climits>
#include <cstring>

RFDMap &RFDMap::getInstance() {
    static RFDMap instance;
    return instance;
}

RFDMap::RFDMap() : next_available_rfd(LAST_RESERVED_RFD_INDEX + 1) {
    InitializeCriticalSection(&mutex);
    addCrtFD(0);
    addCrtFD(1);
    addCrtFD(2);
}

RFD RFDMap::getNextRFDAvailableUnlocked() {
    if (!RFDRecyclePool.empty()) {
        RFD rfd = RFDRecyclePool.front();
        RFDRecyclePool.pop();
        return rfd;
    }
    if (next_available_rfd < INT_MAX)
        return next_available_rfd++;
    return INVALID_FD;
}

RFD RFDMap::addSocket(SOCKET s) {
    EnterCriticalSection(&mutex);
    RFD rfd = INVALID_FD;
    auto existing = SocketToRFDMap.find(s);
    if (existing != SocketToRFDMap.end()) {
        rfd = existing->second;
    } else {
        rfd = getNextRFDAvailableUnlocked();
        if (rfd != INVALID_FD) {
            SocketToRFDMap[s] = rfd;
            SocketInfo info;
            memset(&info, 0, sizeof(info));
            info.socket = s;
            RFDToSocketInfoMap[rfd] = info;
        }
    }
    LeaveCriticalSection(&mutex);
    return rfd;
}

void RFDMap::removeSocketToRFD(SOCKET s) {
    EnterCriticalSection(&mutex);
    SocketToRFDMap.erase(s);
    LeaveCriticalSection(&mutex);
}

void RFDMap::removeRFDToSocketInfo(RFD rfd) {
    EnterCriticalSection(&mutex);
    RFDToSocketInfoMap.erase(rfd);
    RFDRecyclePool.push(rfd);
    LeaveCriticalSection(&mutex);
}

RFD RFDMap::addCrtFD(int crt_fd) {
    EnterCriticalSection(&mutex);
    RFD rfd;
    auto it = CrtFDToRFDMap.find(crt_fd);
    if (it != CrtFDToRFDMap.end()) {
        rfd = it->second;
    } else if (crt_fd >= FIRST_RESERVED_RFD_INDEX &&
               crt_fd <= LAST_RESERVED_RFD_INDEX) {
        rfd = crt_fd;
        CrtFDToRFDMap[crt_fd] = rfd;
        RFDToCrtFDMap[rfd] = crt_fd;
    } else {
        rfd = getNextRFDAvailableUnlocked();
        if (rfd != INVALID_FD) {
            CrtFDToRFDMap[crt_fd] = rfd;
            RFDToCrtFDMap[rfd] = crt_fd;
        }
    }
    LeaveCriticalSection(&mutex);
    return rfd;
}

void RFDMap::removeCrtFD(int crt_fd) {
    if (crt_fd <= LAST_RESERVED_RFD_INDEX)
        return;
    EnterCriticalSection(&mutex);
    auto it = CrtFDToRFDMap.find(crt_fd);
    if (it != CrtFDToRFDMap.end()) {
        RFD rfd = it->second;
        RFDRecyclePool.push(rfd);
        RFDToCrtFDMap.erase(rfd);
        CrtFDToRFDMap.erase(it);
    }
    LeaveCriticalSection(&mutex);
}

SOCKET RFDMap::lookupSocket(RFD rfd) {
    SOCKET s = INVALID_SOCKET;
    EnterCriticalSection(&mutex);
    auto it = RFDToSocketInfoMap.find(rfd);
    if (it != RFDToSocketInfoMap.end())
        s = it->second.socket;
    LeaveCriticalSection(&mutex);
    return s;
}

SocketInfo *RFDMap::lookupSocketInfo(RFD rfd) {
    SocketInfo *info = NULL;
    EnterCriticalSection(&mutex);
    auto it = RFDToSocketInfoMap.find(rfd);
    if (it != RFDToSocketInfoMap.end())
        info = &it->second;
    LeaveCriticalSection(&mutex);
    return info;
}

int RFDMap::lookupCrtFD(RFD rfd) {
    int crt = INVALID_FD;
    EnterCriticalSection(&mutex);
    auto it = RFDToCrtFDMap.find(rfd);
    if (it != RFDToCrtFDMap.end())
        crt = it->second;
    else if (rfd >= FIRST_RESERVED_RFD_INDEX && rfd <= LAST_RESERVED_RFD_INDEX)
        crt = rfd;
    LeaveCriticalSection(&mutex);
    return crt;
}

RFD RFDMap::socketToRFD(SOCKET s) {
    RFD rfd = INVALID_FD;
    EnterCriticalSection(&mutex);
    auto it = SocketToRFDMap.find(s);
    if (it != SocketToRFDMap.end())
        rfd = it->second;
    LeaveCriticalSection(&mutex);
    return rfd;
}
