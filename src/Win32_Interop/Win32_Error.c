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
#include <shellapi.h>
#include "Win32_Error.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

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
    case ERROR_NO_UNICODE_TRANSLATION:      return EILSEQ;
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

int win32_errno_from_system_error(int sys_error) {
    int translated = translate_sys_error(sys_error);
    return translated == -9999 ? EIO : translated;
}

/* */
void set_errno_from_last_error() {
    errno = win32_errno_from_system_error((int)GetLastError());
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
    errno = win32_errno_from_system_error((int)error);
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
    if ((size_t)length > SIZE_MAX / sizeof(*wide)) {
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
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

static int win32_component_equals(const wchar_t *component, size_t length,
                                  const wchar_t *name) {
    size_t index;
    if (wcslen(name) != length) return 0;
    for (index = 0; index < length; index++) {
        if (towupper(component[index]) != towupper(name[index])) return 0;
    }
    return 1;
}

static int win32_component_requires_verbatim(const wchar_t *component,
                                             size_t length) {
    size_t base_length = 0;

    if (length == 0 ||
        (length == 1 && component[0] == L'.') ||
        (length == 2 && component[0] == L'.' && component[1] == L'.'))
        return 0;
    if (component[length - 1] == L'.' || component[length - 1] == L' ')
        return 1;

    while (base_length < length && component[base_length] != L'.' &&
           component[base_length] != L':')
        base_length++;
    while (base_length != 0 && component[base_length - 1] == L' ')
        base_length--;

    if (win32_component_equals(component, base_length, L"CON") ||
        win32_component_equals(component, base_length, L"PRN") ||
        win32_component_equals(component, base_length, L"AUX") ||
        win32_component_equals(component, base_length, L"NUL") ||
        win32_component_equals(component, base_length, L"CONIN$") ||
        win32_component_equals(component, base_length, L"CONOUT$"))
        return 1;
    if (base_length == 4 &&
        ((towupper(component[0]) == L'C' &&
          towupper(component[1]) == L'O' &&
          towupper(component[2]) == L'M') ||
         (towupper(component[0]) == L'L' &&
          towupper(component[1]) == L'P' &&
          towupper(component[2]) == L'T')) &&
        component[3] >= L'1' && component[3] <= L'9')
        return 1;
    return 0;
}

static int win32_path_requires_verbatim(const wchar_t *path) {
    const wchar_t *component = path;
    const wchar_t *cursor = path;

    while (*cursor != L'\0') {
        if (*cursor == L'\\' || *cursor == L'/') {
            if (win32_component_requires_verbatim(
                    component, (size_t)(cursor - component)))
                return 1;
            component = cursor + 1;
        }
        cursor++;
    }
    return win32_component_requires_verbatim(
        component, (size_t)(cursor - component));
}

static wchar_t *win32_utf8_path_to_wide_with_threshold(const char *path,
                                                        size_t threshold) {
    wchar_t *wide = win32_utf8_to_wide(path);
    wchar_t *full;
    size_t length;
    int requires_verbatim;

    if (wide == NULL) return NULL;
    if (wcsncmp(wide, L"\\\\?\\", 4) == 0 ||
        wcsncmp(wide, L"\\\\.\\", 4) == 0) {
        return wide;
    }
    requires_verbatim = win32_path_requires_verbatim(wide);

    full = win32_get_full_path_wide(wide);
    if (full == NULL) {
        free(wide);
        return NULL;
    }
    length = wcslen(full);

    if (length < threshold && !requires_verbatim) {
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

char *win32_get_full_path_utf8(const char *path) {
    wchar_t *wide = win32_utf8_to_wide(path);
    wchar_t *full;
    char *utf8;

    if (wide == NULL) return NULL;
    full = win32_get_full_path_wide(wide);
    free(wide);
    if (full == NULL) return NULL;

    utf8 = win32_wide_to_utf8(full);
    free(full);
    return utf8;
}

int win32_utf8_strings_equal_ignore_case(const char *first, const char *second) {
    wchar_t *wide_first = win32_utf8_to_wide(first);
    wchar_t *wide_second;
    int result;

    if (wide_first == NULL) return 0;
    wide_second = win32_utf8_to_wide(second);
    if (wide_second == NULL) {
        free(wide_first);
        return 0;
    }
    result = CompareStringOrdinal(wide_first, -1, wide_second, -1, TRUE) ==
             CSTR_EQUAL;
    free(wide_second);
    free(wide_first);
    return result;
}

int win32_utf8_contains_ignore_case(const char *value, const char *needle) {
    wchar_t *wide_value = win32_utf8_to_wide(value);
    wchar_t *wide_needle;
    size_t value_length;
    size_t needle_length;
    size_t offset;
    int result = 0;

    if (wide_value == NULL) return 0;
    wide_needle = win32_utf8_to_wide(needle);
    if (wide_needle == NULL) {
        free(wide_value);
        return 0;
    }

    value_length = wcslen(wide_value);
    needle_length = wcslen(wide_needle);
    if (needle_length == 0) {
        result = 1;
    } else if (needle_length <= value_length &&
               needle_length <= INT_MAX) {
        for (offset = 0; offset <= value_length - needle_length; offset++) {
            if (CompareStringOrdinal(wide_value + offset, (int)needle_length,
                                     wide_needle, (int)needle_length, TRUE) ==
                CSTR_EQUAL)
            {
                result = 1;
                break;
            }
        }
    }

    free(wide_needle);
    free(wide_value);
    return result;
}

int win32_utf8_paths_equal(const char *first, const char *second) {
    wchar_t *wide_first = win32_utf8_to_wide(first);
    wchar_t *wide_second;
    wchar_t *cursor;
    int result;

    if (wide_first == NULL) return 0;
    wide_second = win32_utf8_to_wide(second);
    if (wide_second == NULL) {
        free(wide_first);
        return 0;
    }

    for (cursor = wide_first; *cursor != L'\0'; cursor++) {
        if (*cursor == L'/') *cursor = L'\\';
    }
    for (cursor = wide_second; *cursor != L'\0'; cursor++) {
        if (*cursor == L'/') *cursor = L'\\';
    }
    result = CompareStringOrdinal(wide_first, -1, wide_second, -1, TRUE) ==
             CSTR_EQUAL;
    free(wide_second);
    free(wide_first);
    return result;
}

static wchar_t *win32_get_module_filename_for_handle_wide(HMODULE module) {
    DWORD size = 256;

    for (;;) {
        wchar_t *path = (wchar_t *)malloc((size_t)size * sizeof(*path));
        DWORD length;
        if (path == NULL) {
            errno = ENOMEM;
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }

        SetLastError(ERROR_SUCCESS);
        length = GetModuleFileNameW(module, path, size);
        if (length == 0) {
            DWORD error = GetLastError();
            free(path);
            set_errno_from_win32_error(error);
            SetLastError(error);
            return NULL;
        }
        if (length < size) return path;

        free(path);
        if (size > UINT32_MAX / 2) {
            errno = ENAMETOOLONG;
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            return NULL;
        }
        size *= 2;
    }
}

wchar_t *win32_get_module_filename_wide(void) {
    return win32_get_module_filename_for_handle_wide(NULL);
}

char *win32_get_module_filename_utf8(void) {
    wchar_t *wide = win32_get_module_filename_wide();
    char *utf8;
    if (wide == NULL) return NULL;
    utf8 = win32_wide_to_utf8(wide);
    free(wide);
    return utf8;
}

char *win32_get_module_filename_for_handle_utf8(void *module) {
    wchar_t *wide = win32_get_module_filename_for_handle_wide((HMODULE)module);
    char *utf8;
    if (wide == NULL) return NULL;
    utf8 = win32_wide_to_utf8(wide);
    free(wide);
    return utf8;
}

char *win32_get_current_directory_utf8(void) {
    DWORD size = 256;

    for (;;) {
        wchar_t *wide = (wchar_t *)malloc((size_t)size * sizeof(*wide));
        DWORD length;
        char *utf8;
        if (wide == NULL) {
            errno = ENOMEM;
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }

        length = GetCurrentDirectoryW(size, wide);
        if (length == 0) {
            DWORD error = GetLastError();
            free(wide);
            set_errno_from_win32_error(error);
            SetLastError(error);
            return NULL;
        }
        if (length < size) {
            utf8 = win32_wide_to_utf8(wide);
            free(wide);
            return utf8;
        }

        free(wide);
        if (length == UINT32_MAX || length + 1 <= length) {
            errno = ENAMETOOLONG;
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
            return NULL;
        }
        size = length + 1;
    }
}

int win32_set_current_directory_utf8(const char *path) {
    wchar_t *wide = win32_utf8_to_wide(path);
    wchar_t *full;
    BOOL result;
    DWORD error;

    if (wide == NULL) return -1;
    if (wcsncmp(wide, L"\\\\?\\", 4) == 0 ||
        wcsncmp(wide, L"\\\\.\\", 4) == 0 ||
        win32_path_requires_verbatim(wide)) {
        free(wide);
        errno = ENAMETOOLONG;
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return -1;
    }
    full = win32_get_full_path_wide(wide);
    free(wide);
    if (full == NULL) return -1;
    if (wcslen(full) >= MAX_PATH) {
        free(full);
        errno = ENAMETOOLONG;
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return -1;
    }

    result = SetCurrentDirectoryW(full);
    error = result ? ERROR_SUCCESS : GetLastError();
    free(full);
    if (result) return 0;

    set_errno_from_win32_error(error);
    SetLastError(error);
    return -1;
}

char *win32_getcwd(char *buf, size_t size) {
    char *cwd = win32_get_current_directory_utf8();
    size_t length;

    if (cwd == NULL) return NULL;
    if (buf == NULL) return cwd;
    length = strlen(cwd);
    if (length >= size) {
        free(cwd);
        errno = ERANGE;
        return NULL;
    }
    memcpy(buf, cwd, length + 1);
    free(cwd);
    return buf;
}

int win32_get_utf8_argv(int *argc, char ***argv) {
    LPWSTR *wide_argv;
    char **utf8_argv;
    int wide_argc;
    int index;

    if (argc == NULL || argv == NULL) {
        errno = EINVAL;
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
    if (wide_argv == NULL) {
        set_errno_from_last_error();
        return -1;
    }
    if (wide_argc < 0 || (size_t)wide_argc >
                             (SIZE_MAX / sizeof(*utf8_argv)) - 1) {
        LocalFree(wide_argv);
        errno = E2BIG;
        SetLastError(ERROR_META_EXPANSION_TOO_LONG);
        return -1;
    }
    utf8_argv = (char **)calloc((size_t)wide_argc + 1,
                                sizeof(*utf8_argv));
    if (utf8_argv == NULL) {
        LocalFree(wide_argv);
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return -1;
    }
    for (index = 0; index < wide_argc; index++) {
        utf8_argv[index] = win32_wide_to_utf8(wide_argv[index]);
        if (utf8_argv[index] == NULL) {
            DWORD error = GetLastError();
            LocalFree(wide_argv);
            win32_free_utf8_argv(wide_argc, utf8_argv);
            SetLastError(error);
            return -1;
        }
    }
    LocalFree(wide_argv);
    *argc = wide_argc;
    *argv = utf8_argv;
    return 0;
}

void win32_free_utf8_argv(int argc, char **argv) {
    int index;
    if (argv == NULL) return;
    for (index = 0; index < argc; index++) free(argv[index]);
    free(argv);
}

char *win32_getenv_utf8(const char *name) {
    wchar_t *wide_name = win32_utf8_to_wide(name);
    DWORD capacity;

    if (wide_name == NULL) return NULL;
    for (;;) {
        wchar_t *wide_value;
        DWORD length;

        SetLastError(ERROR_SUCCESS);
        capacity = GetEnvironmentVariableW(wide_name, NULL, 0);
        if (capacity == 0) {
            DWORD error = GetLastError();
            free(wide_name);
            if (error == ERROR_ENVVAR_NOT_FOUND) return NULL;
            if (error == ERROR_SUCCESS) return win32_wide_to_utf8(L"");
            set_errno_from_win32_error(error);
            SetLastError(error);
            return NULL;
        }
        wide_value = (wchar_t *)malloc((size_t)capacity * sizeof(*wide_value));
        if (wide_value == NULL) {
            free(wide_name);
            errno = ENOMEM;
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }

        SetLastError(ERROR_SUCCESS);
        length = GetEnvironmentVariableW(wide_name, wide_value, capacity);
        if (length < capacity) {
            char *utf8_value;
            if (length == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
                free(wide_value);
                free(wide_name);
                return NULL;
            }
            utf8_value = win32_wide_to_utf8(wide_value);
            free(wide_value);
            free(wide_name);
            return utf8_value;
        }

        free(wide_value);
    }
}

typedef struct win32_utf8_env_cache_entry {
    char *name;
    char *value;
    struct win32_utf8_env_cache_entry *next;
} win32_utf8_env_cache_entry;

static __declspec(thread) win32_utf8_env_cache_entry *win32_utf8_env_cache;

char *win32_getenv_utf8_cached(const char *name) {
    win32_utf8_env_cache_entry *entry;
    char *value;

    if (name == NULL) {
        errno = EINVAL;
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    value = win32_getenv_utf8(name);
    for (entry = win32_utf8_env_cache; entry != NULL; entry = entry->next) {
        if (win32_utf8_strings_equal_ignore_case(entry->name, name)) {
            free(entry->value);
            entry->value = value;
            return entry->value;
        }
    }

    if (value == NULL) return NULL;
    entry = (win32_utf8_env_cache_entry *)malloc(sizeof(*entry));
    if (entry == NULL) {
        free(value);
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    entry->name = (char *)malloc(strlen(name) + 1);
    if (entry->name == NULL) {
        free(entry);
        free(value);
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    memcpy(entry->name, name, strlen(name) + 1);
    entry->value = value;
    entry->next = win32_utf8_env_cache;
    win32_utf8_env_cache = entry;
    return entry->value;
}

struct win32_utf8_dir {
    HANDLE handle;
    WIN32_FIND_DATAW data;
    int first;
    char *name;
};

win32_utf8_dir *win32_opendir_utf8(const char *path) {
    size_t length;
    char *pattern;
    wchar_t *wide_pattern;
    win32_utf8_dir *dir;

    if (path == NULL) {
        errno = EINVAL;
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    length = strlen(path);
    if (length > SIZE_MAX - 3) {
        errno = ENAMETOOLONG;
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return NULL;
    }
    pattern = (char *)malloc(length + 3);
    if (pattern == NULL) {
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    memcpy(pattern, path, length);
    if (length != 0 && path[length - 1] != '/' && path[length - 1] != '\\')
        pattern[length++] = '/';
    pattern[length++] = '*';
    pattern[length] = '\0';

    wide_pattern = win32_utf8_path_to_wide(pattern);
    free(pattern);
    if (wide_pattern == NULL) return NULL;

    dir = (win32_utf8_dir *)calloc(1, sizeof(*dir));
    if (dir == NULL) {
        free(wide_pattern);
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    dir->handle = FindFirstFileW(wide_pattern, &dir->data);
    free(wide_pattern);
    if (dir->handle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        free(dir);
        set_errno_from_win32_error(error);
        SetLastError(error);
        return NULL;
    }
    dir->first = 1;
    return dir;
}

const char *win32_readdir_utf8(win32_utf8_dir *dir) {
    DWORD error;

    if (dir == NULL || dir->handle == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        SetLastError(ERROR_INVALID_HANDLE);
        return NULL;
    }
    free(dir->name);
    dir->name = NULL;

    if (dir->first) {
        dir->first = 0;
    } else if (!FindNextFileW(dir->handle, &dir->data)) {
        error = GetLastError();
        if (error == ERROR_NO_MORE_FILES) {
            errno = 0;
            return NULL;
        }
        set_errno_from_win32_error(error);
        SetLastError(error);
        return NULL;
    }

    dir->name = win32_wide_to_utf8(dir->data.cFileName);
    return dir->name;
}

int win32_closedir_utf8(win32_utf8_dir *dir) {
    BOOL result;
    DWORD error;

    if (dir == NULL || dir->handle == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        SetLastError(ERROR_INVALID_HANDLE);
        return -1;
    }
    result = FindClose(dir->handle);
    error = result ? ERROR_SUCCESS : GetLastError();
    dir->handle = INVALID_HANDLE_VALUE;
    free(dir->name);
    free(dir);
    if (result) return 0;
    set_errno_from_win32_error(error);
    SetLastError(error);
    return -1;
}

static int compare_utf8_paths(const void *left, const void *right) {
    const char *const *left_path = (const char *const *)left;
    const char *const *right_path = (const char *const *)right;
    return strcmp(*left_path, *right_path);
}

void win32_globfree_utf8(char **paths, size_t count) {
    size_t index;
    if (paths == NULL) return;
    for (index = 0; index < count; index++) free(paths[index]);
    free(paths);
}

typedef struct win32_glob_context {
    char **paths;
    size_t count;
} win32_glob_context;

static int win32_glob_wchar_equal(wchar_t left, wchar_t right) {
    return CompareStringOrdinal(&left, 1, &right, 1, TRUE) == CSTR_EQUAL;
}

static int win32_glob_class_match(const wchar_t *pattern, wchar_t value,
                                  const wchar_t **next_pattern) {
    const wchar_t *current = pattern + 1;
    const wchar_t *end = current;
    int negate = 0;
    int matched = 0;

    if (*end == L'!' || *end == L'^') end++;
    if (*end == L']') end++;
    while (*end != L'\0' && *end != L']') end++;
    if (*end == L'\0') {
        *next_pattern = pattern + 1;
        return win32_glob_wchar_equal(L'[', value);
    }

    if (*current == L'!' || *current == L'^') {
        negate = 1;
        current++;
    }
    if (*current == L']') {
        matched = win32_glob_wchar_equal(L']', value);
        current++;
    }
    while (current < end) {
        wchar_t first = *current++;
        if (current + 1 < end && *current == L'-') {
            wchar_t last = current[1];
            wint_t folded_value = towupper((wint_t)value);
            wint_t folded_first = towupper((wint_t)first);
            wint_t folded_last = towupper((wint_t)last);
            if (folded_first <= folded_last &&
                folded_value >= folded_first && folded_value <= folded_last)
                matched = 1;
            current += 2;
        } else if (win32_glob_wchar_equal(first, value)) {
            matched = 1;
        }
    }

    *next_pattern = end + 1;
    return negate ? !matched : matched;
}

static int win32_glob_component_match(const wchar_t *pattern,
                                      const wchar_t *value) {
    const wchar_t *star_pattern = NULL;
    const wchar_t *star_value = NULL;

    while (*value != L'\0') {
        const wchar_t *next_pattern;
        int matched;

        if (*pattern == L'*') {
            while (*pattern == L'*') pattern++;
            if (*pattern == L'\0') return 1;
            star_pattern = pattern;
            star_value = value;
            continue;
        }
        if (*pattern == L'?') {
            next_pattern = pattern + 1;
            matched = 1;
        } else if (*pattern == L'[') {
            matched = win32_glob_class_match(pattern, *value, &next_pattern);
        } else {
            next_pattern = pattern + (*pattern != L'\0');
            matched = *pattern != L'\0' &&
                      win32_glob_wchar_equal(*pattern, *value);
        }

        if (matched) {
            pattern = next_pattern;
            value++;
            continue;
        }
        if (star_pattern != NULL && *star_value != L'\0') {
            pattern = star_pattern;
            value = ++star_value;
            continue;
        }
        return 0;
    }

    while (*pattern == L'*') pattern++;
    return *pattern == L'\0';
}

static int win32_glob_has_separator(wchar_t value) {
    return value == L'\\' || value == L'/';
}

static int win32_glob_find_component(wchar_t *pattern,
                                     wchar_t **component_start,
                                     wchar_t **component_end) {
    wchar_t *wildcard;
    for (wildcard = pattern; *wildcard != L'\0'; wildcard++) {
        if (*wildcard != L'*' && *wildcard != L'?' && *wildcard != L'[')
            continue;
        *component_start = wildcard;
        while (*component_start > pattern &&
               !win32_glob_has_separator((*component_start)[-1]))
            (*component_start)--;
        *component_end = wildcard;
        while (**component_end != L'\0' &&
               !win32_glob_has_separator(**component_end))
            (*component_end)++;
        return 1;
    }
    return 0;
}

static wchar_t *win32_glob_replace_component(const wchar_t *pattern,
                                             const wchar_t *component_start,
                                             const wchar_t *component_end,
                                             const wchar_t *name) {
    size_t prefix_length = (size_t)(component_start - pattern);
    size_t name_length = wcslen(name);
    size_t suffix_length = wcslen(component_end);
    wchar_t *result;

    if (prefix_length > SIZE_MAX - name_length ||
        prefix_length + name_length > SIZE_MAX - suffix_length - 1) {
        errno = ENAMETOOLONG;
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return NULL;
    }
    result = (wchar_t *)malloc((prefix_length + name_length +
                                suffix_length + 1) * sizeof(*result));
    if (result == NULL) {
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    memcpy(result, pattern, prefix_length * sizeof(*result));
    memcpy(result + prefix_length, name, name_length * sizeof(*result));
    memcpy(result + prefix_length + name_length, component_end,
           (suffix_length + 1) * sizeof(*result));
    return result;
}

static wchar_t *win32_glob_query_for_component(
    const wchar_t *pattern, const wchar_t *component_start) {
    size_t prefix_length = (size_t)(component_start - pattern);
    wchar_t *query;

    if (prefix_length > SIZE_MAX - 2) {
        errno = ENAMETOOLONG;
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return NULL;
    }
    query = (wchar_t *)malloc((prefix_length + 2) * sizeof(*query));
    if (query == NULL) {
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    memcpy(query, pattern, prefix_length * sizeof(*query));
    query[prefix_length] = L'*';
    query[prefix_length + 1] = L'\0';
    return query;
}

static wchar_t *win32_glob_api_path(const wchar_t *path) {
    char *utf8 = win32_wide_to_utf8(path);
    wchar_t *wide;
    if (utf8 == NULL) return NULL;
    wide = win32_utf8_path_to_wide(utf8);
    free(utf8);
    return wide;
}

static int win32_glob_append(win32_glob_context *context,
                             const wchar_t *wide_path) {
    char *path = win32_wide_to_utf8(wide_path);
    char **larger;

    if (path == NULL) return -1;
    if (context->count == SIZE_MAX / sizeof(*context->paths)) {
        free(path);
        errno = E2BIG;
        SetLastError(ERROR_META_EXPANSION_TOO_LONG);
        return -1;
    }
    larger = (char **)realloc(context->paths,
                              (context->count + 1) * sizeof(*context->paths));
    if (larger == NULL) {
        free(path);
        errno = ENOMEM;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return -1;
    }
    context->paths = larger;
    context->paths[context->count++] = path;
    return 0;
}

static int win32_glob_expand(wchar_t *pattern, win32_glob_context *context) {
    wchar_t *component_start;
    wchar_t *component_end;

    if (!win32_glob_find_component(pattern, &component_start,
                                   &component_end)) {
        wchar_t *api_path = win32_glob_api_path(pattern);
        DWORD attributes;
        DWORD error;
        if (api_path == NULL) return -1;
        attributes = GetFileAttributesW(api_path);
        error = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() :
                                                       ERROR_SUCCESS;
        free(api_path);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ||
                error == ERROR_INVALID_NAME)
                return 0;
            set_errno_from_win32_error(error);
            SetLastError(error);
            return -1;
        }
        return win32_glob_append(context, pattern);
    }

    {
        wchar_t saved = *component_end;
        wchar_t *query = win32_glob_query_for_component(pattern, component_start);
        wchar_t *api_query;
        WIN32_FIND_DATAW data;
        HANDLE handle;
        int result = 0;

        if (query == NULL) return -1;
        api_query = win32_glob_api_path(query);
        free(query);
        if (api_query == NULL) return -1;
        handle = FindFirstFileW(api_query, &data);
        free(api_query);
        if (handle == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ||
                error == ERROR_INVALID_NAME)
                return 0;
            set_errno_from_win32_error(error);
            SetLastError(error);
            return -1;
        }

        *component_end = L'\0';
        do {
            wchar_t *expanded;
            if ((wcscmp(data.cFileName, L".") == 0 ||
                 wcscmp(data.cFileName, L"..") == 0) ||
                !win32_glob_component_match(component_start, data.cFileName))
                continue;
            if (saved != L'\0' &&
                (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                continue;

            *component_end = saved;
            expanded = win32_glob_replace_component(pattern, component_start,
                                                    component_end,
                                                    data.cFileName);
            *component_end = L'\0';
            if (expanded == NULL) {
                result = -1;
                break;
            }
            result = win32_glob_expand(expanded, context);
            free(expanded);
            if (result != 0) break;
        } while (FindNextFileW(handle, &data));
        *component_end = saved;

        if (result == 0) {
            DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_FILES) {
                set_errno_from_win32_error(error);
                SetLastError(error);
                result = -1;
            }
        }
        {
            int saved_errno = errno;
            DWORD saved_error = GetLastError();
            FindClose(handle);
            errno = saved_errno;
            SetLastError(saved_error);
        }
        return result;
    }
}

int win32_glob_utf8(const char *pattern, char ***paths, size_t *count) {
    wchar_t *wide_pattern;
    wchar_t *full_pattern;
    win32_glob_context context = {NULL, 0};
    int result;

    if (pattern == NULL || paths == NULL || count == NULL) {
        errno = EINVAL;
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    *paths = NULL;
    *count = 0;
    wide_pattern = win32_utf8_to_wide(pattern);
    if (wide_pattern == NULL) return -1;
    full_pattern = win32_get_full_path_wide(wide_pattern);
    free(wide_pattern);
    if (full_pattern == NULL) return -1;

    result = win32_glob_expand(full_pattern, &context);
    free(full_pattern);
    if (result != 0) {
        win32_globfree_utf8(context.paths, context.count);
        return -1;
    }

    if (context.count > 1)
        qsort(context.paths, context.count, sizeof(*context.paths),
              compare_utf8_paths);
    *paths = context.paths;
    *count = context.count;
    errno = 0;
    SetLastError(ERROR_SUCCESS);
    return 0;
}

#ifdef __cplusplus
}
#endif

