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

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <map>
#include <queue>
#include <windows.h>

#define INVALID_FD -1
typedef int RFD;

typedef struct {
    SOCKET socket;
    void *state;
    int flags;
    SOCKADDR_STORAGE socketAddrStorage;
} SocketInfo;

class RFDMap {
public:
    static RFDMap &getInstance();

    RFD addSocket(SOCKET s);
    void removeSocketToRFD(SOCKET s);
    void removeRFDToSocketInfo(RFD rfd);
    RFD addCrtFD(int crt_fd);
    void removeCrtFD(int crt_fd);
    SOCKET lookupSocket(RFD rfd);
    SocketInfo *lookupSocketInfo(RFD rfd);
    int lookupCrtFD(RFD rfd);
    RFD socketToRFD(SOCKET s);

private:
    RFDMap();
    RFDMap(const RFDMap &);
    void operator=(const RFDMap &);

    RFD getNextRFDAvailableUnlocked();

    std::map<SOCKET, RFD> SocketToRFDMap;
    std::map<int, RFD> CrtFDToRFDMap;
    std::map<RFD, SocketInfo> RFDToSocketInfoMap;
    std::map<RFD, int> RFDToCrtFDMap;
    std::queue<RFD> RFDRecyclePool;
    CRITICAL_SECTION mutex;
    static const int FIRST_RESERVED_RFD_INDEX = 0;
    static const int LAST_RESERVED_RFD_INDEX = 2;
    int next_available_rfd;
};
