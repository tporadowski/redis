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
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WIN32_INTEROP_TYPES_HIREDIS_H
#define WIN32_INTEROP_TYPES_HIREDIS_H

/* POSIX types the Windows CRT does not provide. Do not invent PORT_LONG
 * aliases — keep upstream `long` as `long` (LLP64). Fix the few sizeof(long)
 * / clzl sites in place. */

#ifdef _WIN64
#ifndef _SSIZE_T_DEFINED
typedef __int64           ssize_t;
#define _SSIZE_T_DEFINED
#endif
#else
#ifndef _SSIZE_T_DEFINED
typedef long              ssize_t;
#define _SSIZE_T_DEFINED
#endif
#endif

typedef int               pid_t;

#ifndef mode_t
#define mode_t            unsigned __int32
#endif

#ifndef u_int32_t
typedef unsigned __int32  u_int32_t;
#endif

#endif
