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

#include <Windows.h>
#include "Win32_Error.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef EINPROGRESS
#define EINPROGRESS 112
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/* Converts error codes returned by GetLastError/WSAGetLastError to errno codes */
int translate_sys_error(int sys_error) {
  switch (sys_error) {
    case ERROR_SUCCESS:                     return 0;
    case ERROR_NOACCESS:                    return EACCES;
    case WSAEACCES:                         return EACCES;
    case ERROR_ADDRESS_ALREADY_ASSOCIATED:  return EADDRINUSE;
    case WSAEADDRINUSE:                     return EADDRINUSE;
    case WSAEADDRNOTAVAIL:                  return EADDRNOTAVAIL;
    case WSAEAFNOSUPPORT:                   return EAFNOSUPPORT;
    case WSAEWOULDBLOCK:                    return EAGAIN;
    case WSAEINPROGRESS:                    return EINPROGRESS;
    case WSAEALREADY:                       return EALREADY;
    case ERROR_INVALID_FLAGS:               return EBADF;
    case ERROR_INVALID_HANDLE:              return EBADF;
    case ERROR_LOCK_VIOLATION:              return EBUSY;
    case ERROR_PIPE_BUSY:                   return EBUSY;
    case ERROR_SHARING_VIOLATION:           return EBUSY;
    case ERROR_OPERATION_ABORTED:           return ECANCELED;
    case WSAEINTR:                          return ECANCELED;
    case ERROR_CONNECTION_ABORTED:          return ECONNABORTED;
    case WSAECONNABORTED:                   return ECONNABORTED;
    case ERROR_CONNECTION_REFUSED:          return ECONNREFUSED;
    case WSAECONNREFUSED:                   return ECONNREFUSED;
    case ERROR_NETNAME_DELETED:             return ECONNRESET;
    case WSAECONNRESET:                     return ECONNRESET;
    case ERROR_ALREADY_EXISTS:              return EEXIST;
    case ERROR_FILE_EXISTS:                 return EEXIST;
    case ERROR_BUFFER_OVERFLOW:             return EFAULT;
    case WSAEFAULT:                         return EFAULT;
    case ERROR_HOST_UNREACHABLE:            return EHOSTUNREACH;
    case WSAEHOSTUNREACH:                   return EHOSTUNREACH;
    case ERROR_INSUFFICIENT_BUFFER:         return EINVAL;
    case ERROR_INVALID_DATA:                return EINVAL;
    case ERROR_INVALID_PARAMETER:           return EINVAL;
    case ERROR_SYMLINK_NOT_SUPPORTED:       return EINVAL;
    case WSAEINVAL:                         return EINVAL;
    case WSAEPFNOSUPPORT:                   return EINVAL;
    case WSAESOCKTNOSUPPORT:                return EINVAL;
    case ERROR_BEGINNING_OF_MEDIA:          return EIO;
    case ERROR_BUS_RESET:                   return EIO;
    case ERROR_CRC:                         return EIO;
    case ERROR_DEVICE_DOOR_OPEN:            return EIO;
    case ERROR_DEVICE_REQUIRES_CLEANING:    return EIO;
    case ERROR_DISK_CORRUPT:                return EIO;
    case ERROR_EOM_OVERFLOW:                return EIO;
    case ERROR_FILEMARK_DETECTED:           return EIO;
    case ERROR_GEN_FAILURE:                 return EIO;
    case ERROR_INVALID_BLOCK_LENGTH:        return EIO;
    case ERROR_IO_DEVICE:                   return EIO;
    case ERROR_NO_DATA_DETECTED:            return EIO;
    case ERROR_NO_SIGNAL_SENT:              return EIO;
    case ERROR_OPEN_FAILED:                 return EIO;
    case ERROR_SETMARK_DETECTED:            return EIO;
    case ERROR_SIGNAL_REFUSED:              return EIO;
    case WSAEISCONN:                        return EISCONN;
    case ERROR_CANT_RESOLVE_FILENAME:       return ELOOP;
    case ERROR_TOO_MANY_OPEN_FILES:         return EMFILE;
    case WSAEMFILE:                         return EMFILE;
    case WSAEMSGSIZE:                       return EMSGSIZE;
    case ERROR_FILENAME_EXCED_RANGE:        return ENAMETOOLONG;
    case ERROR_NETWORK_UNREACHABLE:         return ENETUNREACH;
    case WSAENETUNREACH:                    return ENETUNREACH;
    case WSAENOBUFS:                        return ENOBUFS;
    case ERROR_DIRECTORY:                   return ENOENT;
    case ERROR_FILE_NOT_FOUND:              return ENOENT;
    case ERROR_INVALID_NAME:                return ENOENT;
    case ERROR_INVALID_DRIVE:               return ENOENT;
    case ERROR_INVALID_REPARSE_DATA:        return ENOENT;
    case ERROR_MOD_NOT_FOUND:               return ENOENT;
    case ERROR_PATH_NOT_FOUND:              return ENOENT;
    case WSAHOST_NOT_FOUND:                 return ENOENT;
    case WSANO_DATA:                        return ENOENT;
    case ERROR_NOT_ENOUGH_MEMORY:           return ENOMEM;
    case ERROR_OUTOFMEMORY:                 return ENOMEM;
    case ERROR_CANNOT_MAKE:                 return ENOSPC;
    case ERROR_DISK_FULL:                   return ENOSPC;
    case ERROR_EA_TABLE_FULL:               return ENOSPC;
    case ERROR_END_OF_MEDIA:                return ENOSPC;
    case ERROR_HANDLE_DISK_FULL:            return ENOSPC;
    case ERROR_NOT_CONNECTED:               return ENOTCONN;
    case WSAENOTCONN:                       return ENOTCONN;
    case ERROR_DIR_NOT_EMPTY:               return ENOTEMPTY;
    case WSAENOTSOCK:                       return ENOTSOCK;
    case ERROR_NOT_SUPPORTED:               return ENOTSUP;
    case ERROR_BROKEN_PIPE:                 return EPIPE;
    case ERROR_ACCESS_DENIED:               return EPERM;
    case ERROR_PRIVILEGE_NOT_HELD:          return EPERM;
    case ERROR_BAD_PIPE:                    return EPIPE;
    case ERROR_NO_DATA:                     return EPIPE;
    case ERROR_PIPE_NOT_CONNECTED:          return EPIPE;
    case WSAESHUTDOWN:                      return EPIPE;
    case WSAEPROTONOSUPPORT:                return EPROTONOSUPPORT;
    case ERROR_WRITE_PROTECT:               return EROFS;
    case ERROR_SEM_TIMEOUT:                 return ETIMEDOUT;
    case WSAETIMEDOUT:                      return ETIMEDOUT;
    case ERROR_NOT_SAME_DEVICE:             return EXDEV;
    case ERROR_INVALID_FUNCTION:            return EISDIR;
    case ERROR_META_EXPANSION_TOO_LONG:     return E2BIG;
    default:                                return -9999; // to avoid conflicts with other custom codes
  }
}

/* */
void set_errno_from_last_error() {
    errno = translate_sys_error(GetLastError());
}

/* */
int strerror_r(int err, char* buf, size_t buflen) {
    int size = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        err,
        0,
        buf,
        (DWORD) buflen,
        NULL);
    if (size == 0) {
        char* strerr = strerror(err);
        if (strlen(strerr) >= buflen) {
            errno = ERANGE;
            return -1;
        }
        strcpy(buf, strerr);
    }
    if (size > 2 && buf[size - 2] == '\r') {
        /* remove extra CRLF */
        buf[size - 2] = '\0';
    }
    return 0;
}

char wsa_strerror_buf[128];
/* */
char *wsa_strerror(int err) {
    int size = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        err,
        0,
        wsa_strerror_buf,
        128,
        NULL);
    if (size == 0) {
        return strerror(err);
    }
    if (size > 2 && wsa_strerror_buf[size - 2] == '\r') {
        /* remove extra CRLF */
        wsa_strerror_buf[size - 2] = '\0';
    }
    return wsa_strerror_buf;
}

void win32_free(void *value) {
    free(value);
}

static void set_errno_from_win32_error(DWORD error) {
    errno = translate_sys_error((int)error);
    if (errno == -9999)
        errno = EIO;
}

wchar_t *win32_utf8_to_wide(const char *value) {
    int length;
    wchar_t *wide;

    if (value == NULL) {
        errno = EINVAL;
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                 value, -1, NULL, 0);
    if (length == 0) {
        set_errno_from_win32_error(GetLastError());
        return NULL;
    }

    wide = (wchar_t *)malloc((size_t)length * sizeof(*wide));
    if (wide == NULL) {
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            value, -1, wide, length) == 0) {
        DWORD error = GetLastError();
        free(wide);
        set_errno_from_win32_error(error);
        SetLastError(error);
        return NULL;
    }
    return wide;
}

char *win32_wide_to_utf8(const wchar_t *value) {
    int length;
    char *utf8;

    if (value == NULL) {
        errno = EINVAL;
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                 value, -1, NULL, 0, NULL, NULL);
    if (length == 0) {
        set_errno_from_win32_error(GetLastError());
        return NULL;
    }

    utf8 = (char *)malloc((size_t)length);
    if (utf8 == NULL) {
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                            value, -1, utf8, length, NULL, NULL) == 0) {
        DWORD error = GetLastError();
        free(utf8);
        set_errno_from_win32_error(error);
        SetLastError(error);
        return NULL;
    }
    return utf8;
}

static wchar_t *win32_get_full_path_wide(const wchar_t *path) {
    DWORD size = 256;

    for (;;) {
        wchar_t *full = (wchar_t *)malloc((size_t)size * sizeof(*full));
        DWORD length;
        if (full == NULL) {
            errno = ENOMEM;
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }

        length = GetFullPathNameW(path, size, full, NULL);
        if (length == 0) {
            DWORD error = GetLastError();
            free(full);
            set_errno_from_win32_error(error);
            SetLastError(error);
            return NULL;
        }
        if (length < size) return full;

        free(full);
        if (length == UINT32_MAX || length + 1 <= length) {
            errno = ENAMETOOLONG;
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            return NULL;
        }
        size = length + 1;
    }
}

static wchar_t *win32_make_extended_path(wchar_t *full) {
    size_t length = wcslen(full);

    if (wcsncmp(full, L"\\\\", 2) == 0) {
        const wchar_t prefix[] = L"\\\\?\\UNC\\";
        size_t prefix_length = (sizeof(prefix) / sizeof(prefix[0])) - 1;
        wchar_t *extended = (wchar_t *)malloc(
            (prefix_length + length - 2 + 1) * sizeof(*extended));
        if (extended == NULL) {
            free(full);
            errno = ENOMEM;
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }
        memcpy(extended, prefix, prefix_length * sizeof(*extended));
        memcpy(extended + prefix_length, full + 2,
               (length - 2 + 1) * sizeof(*extended));
        free(full);
        return extended;
    } else {
        const wchar_t prefix[] = L"\\\\?\\";
        size_t prefix_length = (sizeof(prefix) / sizeof(prefix[0])) - 1;
        wchar_t *extended = (wchar_t *)malloc(
            (prefix_length + length + 1) * sizeof(*extended));
        if (extended == NULL) {
            free(full);
            errno = ENOMEM;
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }
        memcpy(extended, prefix, prefix_length * sizeof(*extended));
        memcpy(extended + prefix_length, full,
               (length + 1) * sizeof(*extended));
        free(full);
        return extended;
    }
}

static wchar_t *win32_utf8_path_to_wide_with_threshold(const char *path,
                                                        size_t threshold) {
    wchar_t *wide = win32_utf8_to_wide(path);
    wchar_t *full;
    size_t length;

    if (wide == NULL) return NULL;
    if (wcsncmp(wide, L"\\\\?\\", 4) == 0 ||
        wcsncmp(wide, L"\\\\.\\", 4) == 0) {
        return wide;
    }

    full = win32_get_full_path_wide(wide);
    if (full == NULL) {
        free(wide);
        return NULL;
    }
    length = wcslen(full);

    if (length < threshold) {
        free(full);
        return wide;
    }

    free(wide);
    return win32_make_extended_path(full);
}

wchar_t *win32_utf8_path_to_wide(const char *path) {
    return win32_utf8_path_to_wide_with_threshold(path, MAX_PATH);
}

wchar_t *win32_utf8_directory_path_to_wide(const char *path) {
    return win32_utf8_path_to_wide_with_threshold(path, MAX_PATH - 12);
}

#ifdef __cplusplus
}
#endif

