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

#ifndef WIN32_INTEROP_ERROR_H
#define WIN32_INTEROP_ERROR_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef ESOCKTNOSUPPORT
#define ESOCKTNOSUPPORT WSAESOCKTNOSUPPORT /* Socket type not supported */
#endif
#ifndef EPFNOSUPPORT
#define EPFNOSUPPORT    WSAEPFNOSUPPORT    /* Protocol family not supported */
#endif


/* Converts error codes returned by GetLastError/WSAGetLastError to errno codes */
int translate_sys_error(int sys_error);
void set_errno_from_last_error();

/* Redis keeps text and paths as UTF-8. These helpers convert at the Windows
 * Unicode API boundary. Returned buffers are malloc'd; free with win32_free. */
void win32_free(void *value);
wchar_t *win32_utf8_to_wide(const char *value);
char *win32_wide_to_utf8(const wchar_t *value);
wchar_t *win32_utf8_path_to_wide(const char *path);
wchar_t *win32_utf8_directory_path_to_wide(const char *path);

int strerror_r(int err, char* buf, size_t buflen);
char *wsa_strerror(int err);


#ifdef __cplusplus
}
#endif

#endif