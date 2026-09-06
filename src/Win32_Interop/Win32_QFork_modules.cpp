/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/*
 * QFork module-image snapshot/restore, taken from pwin32
 * src/Win32_Interop/Win32_QFork.cpp (Prepare/Inspect/Restore module
 * images) and adapted for redis_win's payload-map child. No PORT_LONG,
 * no SmartHandle, no pwin32 control-block globals.
 */
#define QFORK_MAIN_IMPL
#include "Win32_QFork.h"

#ifdef __cplusplus
extern "C" {
#endif
void serverLogRaw(int level, const char *msg);
#ifdef __cplusplus
}
#endif
#ifndef LL_DEBUG
#define LL_DEBUG 0
#endif
#ifndef LL_WARNING
#define LL_WARNING 3
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#include <stdarg.h>
#include <stdio.h>

static int g_qfork_module_use_server_log = 1;

static void qforkModuleLog(int level, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (g_qfork_module_use_server_log)
        serverLogRaw(level, buf);
    else
        fprintf(stderr, "%s\n", buf);
}
#define serverLog qforkModuleLog

extern "C" void QForkModuleLogToStderr(void) {
    g_qfork_module_use_server_log = 0;
}

#ifdef inline
#undef inline
#endif
#ifdef static_assert
#undef static_assert
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef byte
#undef byte
#endif

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

struct CloseHandleGuard {
    HANDLE h;
    explicit CloseHandleGuard(HANDLE handle) : h(handle) {}
    ~CloseHandleGuard() {
        if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    CloseHandleGuard(const CloseHandleGuard &) = delete;
    CloseHandleGuard &operator=(const CloseHandleGuard &) = delete;
};

using std::exception;
using std::max;
using std::min;
using std::numeric_limits;
using std::runtime_error;
using std::string;
using std::system_category;
using std::system_error;
using std::vector;
using std::wstring;

extern "C" int moduleForEachForkModule(
    int (*callback)(const char *name, void *handle, const wchar_t *path,
                    uint64_t sequence, void *privdata),
    void *privdata);

typedef unsigned char byte;

static HANDLE g_hQForkModuleSnapshotMap;
static uint64_t g_QForkModuleSnapshotSize;
static uint32_t g_QForkModuleSnapshotCount;
static vector<HMODULE> g_QForkPinnedModules;
static vector<HANDLE> g_QForkPinnedModuleFiles;
struct QForkModuleDescriptor {
    string name;
    wstring path;
    HMODULE expectedBase;
    uint64_t sequence;
};

struct QForkModuleFileIdentity {
    DWORD volumeSerialNumber;
    DWORD fileIndexHigh;
    DWORD fileIndexLow;
    uint64_t fileSize;
    uint64_t lastWriteTime;
};

struct QForkModuleProtectedRange {
    DWORD rva;
    size_t size;
};

struct QForkModuleImageRange {
    byte *address;
    DWORD rva;
    DWORD size;
    DWORD characteristics;
    WORD sectionIndex;
    char name[IMAGE_SIZEOF_SHORT_NAME];
    uint64_t snapshotOffset;
};

struct QForkModuleImageInfo {
    DWORD imageSize;
    DWORD sizeOfHeaders;
    DWORD timeDateStamp;
    DWORD checkSum;
    WORD machine;
    WORD optionalMagic;
    WORD numberOfSections;
    QForkModuleFileIdentity fileIdentity;
    vector<QForkModuleProtectedRange> protectedRanges;
    vector<QForkModuleImageRange> writableRanges;
};

struct QForkCapturedModule {
    QForkModuleDescriptor descriptor;
    QForkModuleImageInfo image;
    uint64_t nameOffset;
    uint64_t pathOffset;
    uint32_t firstRange;
};

static const uint32_t cQForkModuleSnapshotMagic = 0x534d4651; /* QFMS */
static const uint16_t cQForkModuleSnapshotVersion = 1;
static const uint64_t cQForkModuleTlsTemplateSize = sizeof(uintptr_t);
static const size_t cQForkModuleMaxTlsCallbacks = 32;

struct QForkModuleSnapshotHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t headerSize;
    uint32_t moduleCount;
    uint32_t rangeCount;
    uint64_t totalSize;
    uint64_t modulesOffset;
    uint64_t rangesOffset;
    uint64_t payloadOffset;
};

struct QForkModuleSnapshotRecord {
    uint64_t sequence;
    uint64_t expectedBase;
    uint64_t nameOffset;
    uint64_t pathOffset;
    uint32_t nameBytes;
    uint32_t pathChars;
    uint32_t firstRange;
    uint32_t rangeCount;
    uint32_t imageSize;
    uint32_t sizeOfHeaders;
    uint32_t timeDateStamp;
    uint32_t checkSum;
    uint16_t machine;
    uint16_t optionalMagic;
    uint16_t numberOfSections;
    uint16_t reserved;
    uint32_t volumeSerialNumber;
    uint32_t fileIndexHigh;
    uint32_t fileIndexLow;
    uint32_t fileIdentityReserved;
    uint64_t fileSize;
    uint64_t lastWriteTime;
};

struct QForkModuleSnapshotRange {
    uint64_t bytesOffset;
    uint32_t rva;
    uint32_t size;
    uint32_t characteristics;
    uint16_t sectionIndex;
    uint16_t reserved;
    char name[IMAGE_SIZEOF_SHORT_NAME];
};

static int CollectQForkModule(const char *name, void *handle,
    const wchar_t *path, uint64_t sequence, void *privdata)
{
    try {
        vector<QForkModuleDescriptor> *modules =
            reinterpret_cast<vector<QForkModuleDescriptor> *>(privdata);
        QForkModuleDescriptor descriptor;
        descriptor.name = name;
        descriptor.path = path;
        descriptor.expectedBase = reinterpret_cast<HMODULE>(handle);
        descriptor.sequence = sequence;
        modules->push_back(descriptor);
        return 0;
    }
    catch (...) {
        return -1;
    }
}

static bool QForkModuleRangeInsideImage(uint64_t rva, uint64_t size,
    uint64_t imageSize)
{
    return rva <= imageSize && size <= imageSize - rva;
}

static bool QForkModuleRangeInsideRange(uint64_t rva, uint64_t size,
    uint64_t outerRva, uint64_t outerSize)
{
    return rva >= outerRva && rva - outerRva <= outerSize &&
           size <= outerSize - (rva - outerRva);
}

static bool QForkCheckedAdd(uint64_t left, uint64_t right, uint64_t *result)
{
    if (right > numeric_limits<uint64_t>::max() - left) return false;
    *result = left + right;
    return true;
}

static bool QForkCheckedMultiply(uint64_t left, uint64_t right,
    uint64_t *result)
{
    if (left != 0 && right > numeric_limits<uint64_t>::max() / left)
        return false;
    *result = left * right;
    return true;
}

static bool QForkCheckedAlign(uint64_t value, uint64_t alignment,
    uint64_t *result)
{
    if (alignment == 0) return false;
    uint64_t remainder = value % alignment;
    if (remainder == 0) {
        *result = value;
        return true;
    }
    return QForkCheckedAdd(value, alignment - remainder, result);
}

static bool AddQForkModuleProtectedRange(
    vector<QForkModuleProtectedRange>& ranges,
    uint64_t rva, uint64_t size, uint64_t imageSize)
{
    if (size == 0 || !QForkModuleRangeInsideImage(rva, size, imageSize) ||
        rva > MAXDWORD || size > MAXDWORD)
        return false;

    QForkModuleProtectedRange range;
    range.rva = static_cast<DWORD>(rva);
    range.size = static_cast<size_t>(size);
    ranges.push_back(range);
    return true;
}

static void RestoreQForkModuleProtectedRanges(byte *imageBase,
    DWORD sectionRva, vector<byte>& childBytes,
    const vector<QForkModuleProtectedRange>& protectedRanges)
{
    uint64_t sectionStart = sectionRva;
    uint64_t sectionEnd = sectionStart + childBytes.size();

    for (const QForkModuleProtectedRange& range : protectedRanges) {
        uint64_t protectedStart = range.rva;
        uint64_t protectedEnd = protectedStart + range.size;
        uint64_t copyStart = max(sectionStart, protectedStart);
        uint64_t copyEnd = min(sectionEnd, protectedEnd);
        if (copyStart >= copyEnd) continue;

        memcpy(imageBase + copyStart,
               childBytes.data() + (copyStart - sectionStart),
               static_cast<size_t>(copyEnd - copyStart));
    }
}

static bool GetQForkModuleFileIdentity(HANDLE file,
    QForkModuleFileIdentity& identity, const char *name)
{
    BY_HANDLE_FILE_INFORMATION info;
    BOOL ok = GetFileInformationByHandle(file, &info);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    if (!ok) {
        serverLog(LL_WARNING,
            "QFork module validation: could not identify %s image (0x%08x)",
            name, error);
        return false;
    }
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        serverLog(LL_WARNING,
            "QFork module validation: %s image path is a directory", name);
        return false;
    }

    ULARGE_INTEGER size;
    size.LowPart = info.nFileSizeLow;
    size.HighPart = info.nFileSizeHigh;
    ULARGE_INTEGER writeTime;
    writeTime.LowPart = info.ftLastWriteTime.dwLowDateTime;
    writeTime.HighPart = info.ftLastWriteTime.dwHighDateTime;
    identity.volumeSerialNumber = info.dwVolumeSerialNumber;
    identity.fileIndexHigh = info.nFileIndexHigh;
    identity.fileIndexLow = info.nFileIndexLow;
    identity.fileSize = size.QuadPart;
    identity.lastWriteTime = writeTime.QuadPart;
    return true;
}

static HANDLE OpenQForkModuleFile(const wchar_t *path,
    wstring& canonicalPath, const char *name)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        serverLog(LL_WARNING,
            "QFork module validation: could not lock %s image (0x%08x)",
            name, GetLastError());
        return NULL;
    }

    if (GetFileType(file) != FILE_TYPE_DISK) {
        serverLog(LL_WARNING,
            "QFork module validation: %s image is not a disk file", name);
        CloseHandle(file);
        return NULL;
    }

    try {
        DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
        DWORD required = GetFinalPathNameByHandleW(file, NULL, 0, flags);
        if (required == 0 || required == MAXDWORD) {
            DWORD error = required == 0 ? GetLastError() :
                ERROR_INSUFFICIENT_BUFFER;
            serverLog(LL_WARNING,
                "QFork module validation: could not resolve %s image path (0x%08x)",
                name, error);
            CloseHandle(file);
            return NULL;
        }

        vector<wchar_t> pathBuffer(static_cast<size_t>(required) + 1);
        DWORD copied = GetFinalPathNameByHandleW(file, pathBuffer.data(),
            static_cast<DWORD>(pathBuffer.size()), flags);
        if (copied == 0 || copied >= pathBuffer.size()) {
            DWORD error = copied == 0 ? GetLastError() :
                ERROR_INSUFFICIENT_BUFFER;
            serverLog(LL_WARNING,
                "QFork module validation: could not canonicalize %s image path (0x%08x)",
                name, error);
            CloseHandle(file);
            return NULL;
        }

        canonicalPath.assign(pathBuffer.data(), copied);
        return file;
    }
    catch (...) {
        CloseHandle(file);
        throw;
    }
}

static bool QForkModuleFileIdentityEqual(
    const QForkModuleFileIdentity& left,
    const QForkModuleFileIdentity& right)
{
    return left.volumeSerialNumber == right.volumeSerialNumber &&
           left.fileIndexHigh == right.fileIndexHigh &&
           left.fileIndexLow == right.fileIndexLow &&
           left.fileSize == right.fileSize &&
           left.lastWriteTime == right.lastWriteTime;
}

static bool GetQForkModuleHeaders(HMODULE module, const char *name,
    PIMAGE_NT_HEADERS64 *ntOut, DWORD *imageSizeOut)
{
    MODULEINFO moduleInfo;
    if (!GetModuleInformation(GetCurrentProcess(), module, &moduleInfo,
                              sizeof(moduleInfo)))
    {
        serverLog(LL_WARNING,
            "QFork module validation: GetModuleInformation failed for %s (0x%08x)",
            name, GetLastError());
        return false;
    }

    byte *imageBase = reinterpret_cast<byte *>(moduleInfo.lpBaseOfDll);
    PIMAGE_DOS_HEADER dos = reinterpret_cast<PIMAGE_DOS_HEADER>(imageBase);
    if (moduleInfo.lpBaseOfDll != module ||
        moduleInfo.SizeOfImage < sizeof(*dos) ||
        dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        moduleInfo.SizeOfImage < sizeof(IMAGE_NT_HEADERS64) ||
        static_cast<uint64_t>(dos->e_lfanew) >
            moduleInfo.SizeOfImage - sizeof(IMAGE_NT_HEADERS64))
    {
        serverLog(LL_WARNING,
            "QFork module validation: invalid DOS header for %s", name);
        return false;
    }

    PIMAGE_NT_HEADERS64 nt = reinterpret_cast<PIMAGE_NT_HEADERS64>(
        imageBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.SizeOfImage != moduleInfo.SizeOfImage ||
        nt->OptionalHeader.NumberOfRvaAndSizes >
            IMAGE_NUMBEROF_DIRECTORY_ENTRIES ||
        nt->OptionalHeader.SizeOfHeaders > moduleInfo.SizeOfImage ||
        nt->FileHeader.NumberOfSections == 0)
    {
        serverLog(LL_WARNING,
            "QFork module validation: unsupported PE image for %s", name);
        return false;
    }

    uint64_t sectionTableOffset = static_cast<uint64_t>(dos->e_lfanew) +
        sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) +
        nt->FileHeader.SizeOfOptionalHeader;
    uint64_t sectionTableSize;
    uint64_t sectionTableEnd;
    if (!QForkCheckedMultiply(nt->FileHeader.NumberOfSections,
                             sizeof(IMAGE_SECTION_HEADER),
                             &sectionTableSize) ||
        !QForkCheckedAdd(sectionTableOffset, sectionTableSize,
                         &sectionTableEnd) ||
        sectionTableEnd > nt->OptionalHeader.SizeOfHeaders ||
        sectionTableEnd > moduleInfo.SizeOfImage)
    {
        serverLog(LL_WARNING,
            "QFork module validation: invalid section table for %s", name);
        return false;
    }

    *ntOut = nt;
    *imageSizeOut = moduleInfo.SizeOfImage;
    return true;
}

static IMAGE_DATA_DIRECTORY GetQForkModuleDataDirectory(
    PIMAGE_NT_HEADERS64 nt, DWORD index)
{
    IMAGE_DATA_DIRECTORY empty = { 0, 0 };
    if (index >= IMAGE_NUMBEROF_DIRECTORY_ENTRIES ||
        index >= nt->OptionalHeader.NumberOfRvaAndSizes)
        return empty;
    return nt->OptionalHeader.DataDirectory[index];
}

static bool ValidateQForkModuleImportThunks(HMODULE module, DWORD imageSize,
    const IMAGE_DATA_DIRECTORY& imports, const IMAGE_DATA_DIRECTORY& iat,
    const char *name)
{
    bool importsPresent = imports.VirtualAddress != 0 || imports.Size != 0;
    bool iatPresent = iat.VirtualAddress != 0 || iat.Size != 0;
    if (!importsPresent) return true;

    if (imports.VirtualAddress == 0 || imports.Size == 0 ||
        iat.VirtualAddress == 0 || iat.Size == 0 ||
        imports.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR) ||
        !QForkModuleRangeInsideImage(imports.VirtualAddress, imports.Size,
                                     imageSize) ||
        !QForkModuleRangeInsideImage(iat.VirtualAddress, iat.Size, imageSize))
    {
        serverLog(LL_WARNING,
            "QFork module validation: invalid import/IAT directory in %s",
            name);
        return false;
    }

    byte *imageBase = reinterpret_cast<byte *>(module);
    size_t descriptorCount = imports.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
    bool descriptorsTerminated = false;
    for (size_t i = 0; i < descriptorCount; i++) {
        IMAGE_IMPORT_DESCRIPTOR descriptor;
        memcpy(&descriptor,
            imageBase + imports.VirtualAddress +
                i * sizeof(IMAGE_IMPORT_DESCRIPTOR),
            sizeof(descriptor));

        if (descriptor.OriginalFirstThunk == 0 &&
            descriptor.TimeDateStamp == 0 &&
            descriptor.ForwarderChain == 0 && descriptor.Name == 0 &&
            descriptor.FirstThunk == 0)
        {
            descriptorsTerminated = true;
            break;
        }

        if (descriptor.FirstThunk == 0 ||
            descriptor.FirstThunk % sizeof(IMAGE_THUNK_DATA64) != 0 ||
            !QForkModuleRangeInsideRange(descriptor.FirstThunk,
                sizeof(IMAGE_THUNK_DATA64), iat.VirtualAddress, iat.Size))
        {
            serverLog(LL_WARNING,
                "QFork module validation: import thunk outside IAT in %s",
                name);
            return false;
        }

        uint64_t available = static_cast<uint64_t>(iat.VirtualAddress) +
            iat.Size - descriptor.FirstThunk;
        size_t thunkCount = static_cast<size_t>(
            available / sizeof(IMAGE_THUNK_DATA64));
        bool thunksTerminated = false;
        for (size_t j = 0; j < thunkCount; j++) {
            IMAGE_THUNK_DATA64 thunk;
            memcpy(&thunk,
                imageBase + descriptor.FirstThunk +
                    j * sizeof(IMAGE_THUNK_DATA64),
                sizeof(thunk));
            if (thunk.u1.Function == 0) {
                thunksTerminated = true;
                break;
            }
        }
        if (!thunksTerminated) {
            serverLog(LL_WARNING,
                "QFork module validation: unterminated import thunk array in %s",
                name);
            return false;
        }
    }

    if (!descriptorsTerminated) {
        serverLog(LL_WARNING,
            "QFork module validation: unterminated import directory in %s",
            name);
        return false;
    }
    return iatPresent;
}

static bool InspectQForkModuleImage(HMODULE module, HANDLE moduleFile,
    const char *name, QForkModuleImageInfo& image)
{
    const char *safeName = name != NULL ? name : "unknown";
    if (module == NULL || moduleFile == NULL ||
        moduleFile == INVALID_HANDLE_VALUE)
    {
        serverLog(LL_WARNING,
            "QFork module validation: missing image handle for %s", safeName);
        return false;
    }

    PIMAGE_NT_HEADERS64 nt;
    DWORD imageSize;
    if (!GetQForkModuleHeaders(module, safeName, &nt, &imageSize) ||
        !GetQForkModuleFileIdentity(moduleFile, image.fileIdentity, safeName))
        return false;

    image.imageSize = imageSize;
    image.sizeOfHeaders = nt->OptionalHeader.SizeOfHeaders;
    image.timeDateStamp = nt->FileHeader.TimeDateStamp;
    image.checkSum = nt->OptionalHeader.CheckSum;
    image.machine = nt->FileHeader.Machine;
    image.optionalMagic = nt->OptionalHeader.Magic;
    image.numberOfSections = nt->FileHeader.NumberOfSections;

    IMAGE_DATA_DIRECTORY delayImports = GetQForkModuleDataDirectory(nt,
        IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT);
    if (delayImports.VirtualAddress != 0 || delayImports.Size != 0) {
        serverLog(LL_WARNING,
            "QFork module validation: delay imports are unsupported in %s",
            safeName);
        return false;
    }

    uint64_t base = reinterpret_cast<uint64_t>(module);

    IMAGE_DATA_DIRECTORY imports = GetQForkModuleDataDirectory(nt,
        IMAGE_DIRECTORY_ENTRY_IMPORT);
    IMAGE_DATA_DIRECTORY iat = GetQForkModuleDataDirectory(nt,
        IMAGE_DIRECTORY_ENTRY_IAT);
    if (iat.VirtualAddress != 0 || iat.Size != 0) {
        if (iat.VirtualAddress == 0 || iat.Size == 0 ||
            !AddQForkModuleProtectedRange(image.protectedRanges,
                iat.VirtualAddress, iat.Size, imageSize))
        {
            serverLog(LL_WARNING,
                "QFork module validation: invalid IAT range in %s", safeName);
            return false;
        }
    }
    if (!ValidateQForkModuleImportThunks(module, imageSize, imports, iat,
                                          safeName))
        return false;

    if (nt->OptionalHeader.DllCharacteristics &
        IMAGE_DLLCHARACTERISTICS_GUARD_CF)
    {
        serverLog(LL_WARNING,
            "QFork module validation: CFG/XFG is unsupported in %s",
            safeName);
        return false;
    }

    IMAGE_DATA_DIRECTORY tlsDirectory = GetQForkModuleDataDirectory(nt,
        IMAGE_DIRECTORY_ENTRY_TLS);
    if (tlsDirectory.VirtualAddress != 0 || tlsDirectory.Size != 0) {
        if (tlsDirectory.VirtualAddress == 0 ||
            tlsDirectory.Size < sizeof(IMAGE_TLS_DIRECTORY64) ||
            !QForkModuleRangeInsideImage(tlsDirectory.VirtualAddress,
                sizeof(IMAGE_TLS_DIRECTORY64), imageSize))
        {
            serverLog(LL_WARNING,
                "QFork module validation: invalid TLS directory in %s",
                safeName);
            return false;
        }
        PIMAGE_TLS_DIRECTORY64 tls =
            reinterpret_cast<PIMAGE_TLS_DIRECTORY64>(
                reinterpret_cast<byte *>(module) + tlsDirectory.VirtualAddress);
        uint64_t tlsTemplateRva = tls->StartAddressOfRawData - base;
        if (tls->StartAddressOfRawData < base ||
            tls->EndAddressOfRawData < tls->StartAddressOfRawData ||
            tls->EndAddressOfRawData - tls->StartAddressOfRawData !=
                cQForkModuleTlsTemplateSize ||
            tls->SizeOfZeroFill != 0 ||
            !AddQForkModuleProtectedRange(image.protectedRanges,
                tlsTemplateRva,
                tls->EndAddressOfRawData - tls->StartAddressOfRawData,
                imageSize))
        {
            serverLog(LL_WARNING,
                "QFork module validation: non-boilerplate TLS data in %s",
                safeName);
            return false;
        }
        const byte *tlsTemplate = reinterpret_cast<const byte *>(module) +
            tlsTemplateRva;
        for (size_t i = 0; i < cQForkModuleTlsTemplateSize; i++) {
            if (tlsTemplate[i] != 0) {
                serverLog(LL_WARNING,
                    "QFork module validation: initialized TLS data in %s",
                    safeName);
                return false;
            }
        }
        if (tls->AddressOfIndex < base ||
            !AddQForkModuleProtectedRange(image.protectedRanges,
                tls->AddressOfIndex - base, sizeof(DWORD), imageSize))
        {
            serverLog(LL_WARNING,
                "QFork module validation: invalid TLS index in %s", safeName);
            return false;
        }

        if (tls->AddressOfCallBacks != 0) {
            if (tls->AddressOfCallBacks < base ||
                !QForkModuleRangeInsideImage(tls->AddressOfCallBacks - base,
                    sizeof(uint64_t), imageSize))
            {
                serverLog(LL_WARNING,
                    "QFork module validation: invalid TLS callback table in %s",
                    safeName);
                return false;
            }
            bool terminated = false;
            uint64_t callbacksRva = tls->AddressOfCallBacks - base;
            for (size_t i = 0; i < cQForkModuleMaxTlsCallbacks; i++) {
                uint64_t entryRva = callbacksRva + i * sizeof(uint64_t);
                if (!QForkModuleRangeInsideImage(entryRva,
                                                  sizeof(uint64_t), imageSize))
                    break;
                uint64_t callback = *reinterpret_cast<uint64_t *>(
                    reinterpret_cast<byte *>(module) + entryRva);
                if (callback == 0) {
                    terminated = true;
                    break;
                }
                if (callback < base || callback - base >= imageSize) break;
            }
            if (!terminated) {
                serverLog(LL_WARNING,
                    "QFork module validation: unsupported TLS callbacks in %s",
                    safeName);
                return false;
            }
        }
    }

    IMAGE_DATA_DIRECTORY loadConfigDirectory = GetQForkModuleDataDirectory(nt,
        IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG);
    if (loadConfigDirectory.VirtualAddress != 0 ||
        loadConfigDirectory.Size != 0)
    {
        const uint64_t securityCookieEnd =
            FIELD_OFFSET(IMAGE_LOAD_CONFIG_DIRECTORY64, SecurityCookie) +
                sizeof(uint64_t);
        if (loadConfigDirectory.VirtualAddress == 0 ||
            loadConfigDirectory.Size < securityCookieEnd ||
            !QForkModuleRangeInsideImage(loadConfigDirectory.VirtualAddress,
                securityCookieEnd, imageSize))
        {
            serverLog(LL_WARNING,
                "QFork module validation: invalid load config in %s",
                safeName);
            return false;
        }
        const byte *loadConfig = reinterpret_cast<const byte *>(module) +
            loadConfigDirectory.VirtualAddress;
        DWORD declaredLoadConfigSize;
        memcpy(&declaredLoadConfigSize,
            loadConfig + FIELD_OFFSET(IMAGE_LOAD_CONFIG_DIRECTORY64, Size),
            sizeof(declaredLoadConfigSize));
        uint64_t loadConfigSize = declaredLoadConfigSize;
        if (loadConfigSize < securityCookieEnd ||
            loadConfigSize > loadConfigDirectory.Size ||
            loadConfigSize > sizeof(IMAGE_LOAD_CONFIG_DIRECTORY64) ||
            !QForkModuleRangeInsideImage(loadConfigDirectory.VirtualAddress,
                loadConfigSize, imageSize))
        {
            serverLog(LL_WARNING,
                "QFork module validation: inconsistent load config in %s",
                safeName);
            return false;
        }
        uint64_t securityCookie;
        memcpy(&securityCookie,
            loadConfig + FIELD_OFFSET(IMAGE_LOAD_CONFIG_DIRECTORY64,
                                      SecurityCookie),
            sizeof(securityCookie));
        if (securityCookie != 0 &&
            (securityCookie < base ||
             !AddQForkModuleProtectedRange(image.protectedRanges,
                securityCookie - base, sizeof(uint64_t), imageSize)))
        {
            serverLog(LL_WARNING,
                "QFork module validation: invalid security cookie in %s",
                safeName);
            return false;
        }
        /* clang-cl/MSVC still emit GuardCF* slots when CFG is off. Reject
         * only IMAGE_DLLCHARACTERISTICS_GUARD_CF above; the tail is not
         * snapshotted. */
    }

    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(
        reinterpret_cast<PIMAGE_NT_HEADERS>(nt));
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
        if (section->Characteristics &
            (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_DISCARDABLE | IMAGE_SCN_MEM_SHARED))
        {
            serverLog(LL_WARNING,
                "QFork module validation: unsafe writable section in %s",
                safeName);
            return false;
        }

        DWORD sectionSize = max(section->Misc.VirtualSize,
                                section->SizeOfRawData);
        if (sectionSize == 0) continue;
        if (!QForkModuleRangeInsideImage(section->VirtualAddress,
                                          sectionSize, imageSize))
        {
            serverLog(LL_WARNING,
                "QFork module validation: invalid section bounds in %s",
                safeName);
            return false;
        }

        QForkModuleImageRange writable;
        writable.address = reinterpret_cast<byte *>(module) +
            section->VirtualAddress;
        writable.rva = section->VirtualAddress;
        writable.size = sectionSize;
        writable.characteristics = section->Characteristics;
        writable.sectionIndex = i;
        memcpy(writable.name, section->Name, sizeof(writable.name));
        writable.snapshotOffset = 0;
        image.writableRanges.push_back(writable);
    }

    return true;
}

extern "C" int QForkValidateModuleImage(void *handle, const wchar_t *path,
    const char *name)
{
    try {
        const char *safeName = name != NULL ? name : "unknown";
        if (path == NULL || path[0] == L'\0') return 0;
        wstring canonicalPath;
        HANDLE moduleFile = OpenQForkModuleFile(path, canonicalPath, safeName);
        if (moduleFile == NULL) return 0;
        CloseHandleGuard pinnedFile(moduleFile);
        QForkModuleImageInfo image;
        return InspectQForkModuleImage(reinterpret_cast<HMODULE>(handle),
            moduleFile, name, image) ? 1 : 0;
    }
    catch (const exception& ex) {
        serverLog(LL_WARNING,
            "QFork module validation: exception for %s: %s",
            name != NULL ? name : "unknown", ex.what());
        return 0;
    }
    catch (...) {
        serverLog(LL_WARNING,
            "QFork module validation: unknown exception for %s",
            name != NULL ? name : "unknown");
        return 0;
    }
}

static void ReleasePinnedQForkModules(vector<HMODULE>& modules)
{
    for (vector<HMODULE>::reverse_iterator module = modules.rbegin();
         module != modules.rend(); ++module)
    {
        if (*module != NULL) FreeLibrary(*module);
    }
    modules.clear();
}

static bool ReleasePinnedQForkModuleFiles(vector<HANDLE>& files)
{
    bool success = true;
    for (vector<HANDLE>::reverse_iterator file = files.rbegin();
         file != files.rend(); ++file)
    {
        if (*file != NULL && *file != INVALID_HANDLE_VALUE &&
            !CloseHandle(*file))
            success = false;
    }
    files.clear();
    return success;
}

static bool ReleaseQForkModuleSnapshot()
{
    bool success = true;
    if (g_hQForkModuleSnapshotMap != NULL) {
        if (!CloseHandle(g_hQForkModuleSnapshotMap)) success = false;
        g_hQForkModuleSnapshotMap = NULL;
    }
    g_QForkModuleSnapshotSize = 0;
    g_QForkModuleSnapshotCount = 0;
    for (vector<HMODULE>::reverse_iterator module =
             g_QForkPinnedModules.rbegin();
         module != g_QForkPinnedModules.rend(); ++module)
    {
        if (*module != NULL && !FreeLibrary(*module)) success = false;
    }
    g_QForkPinnedModules.clear();
    if (!ReleasePinnedQForkModuleFiles(g_QForkPinnedModuleFiles))
        success = false;
    return success;
}

static void PrepareQForkModuleSnapshot()
{
    if (g_hQForkModuleSnapshotMap != NULL ||
        !g_QForkPinnedModules.empty() ||
        !g_QForkPinnedModuleFiles.empty())
        throw runtime_error("A QFork module snapshot is already active");

    g_QForkModuleSnapshotSize = 0;
    g_QForkModuleSnapshotCount = 0;

    vector<QForkModuleDescriptor> descriptors;
    if (moduleForEachForkModule(CollectQForkModule, &descriptors) != 0)
        throw runtime_error("Could not enumerate modules for QFork snapshot");
    if (descriptors.empty()) return;

    sort(descriptors.begin(), descriptors.end(),
        [](const QForkModuleDescriptor& left,
           const QForkModuleDescriptor& right) {
            return left.sequence < right.sequence;
        });

    if (descriptors.size() > numeric_limits<uint32_t>::max())
        throw runtime_error("Too many modules for QFork snapshot");

    vector<QForkCapturedModule> modules;
    vector<HMODULE> pinnedModules;
    vector<HANDLE> pinnedModuleFiles;
    HANDLE writableMap = NULL;
    HANDLE readOnlyMap = NULL;
    byte *view = NULL;

    try {
        modules.reserve(descriptors.size());
        pinnedModules.reserve(descriptors.size());
        pinnedModuleFiles.reserve(descriptors.size());
        uint64_t totalRanges = 0;
        for (size_t i = 0; i < descriptors.size(); i++) {
            if (i != 0 &&
                descriptors[i - 1].sequence >= descriptors[i].sequence)
                throw runtime_error("Duplicate QFork module load sequence");
            for (size_t j = 0; j < i; j++) {
                if (descriptors[j].expectedBase == descriptors[i].expectedBase)
                    throw runtime_error("Duplicate QFork module image base");
            }

            HMODULE pinned = NULL;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                    reinterpret_cast<LPCWSTR>(descriptors[i].expectedBase),
                    &pinned)) {
                throw system_error(GetLastError(), system_category(),
                    "Could not pin QFork module image");
            }
            if (pinned != descriptors[i].expectedBase) {
                FreeLibrary(pinned);
                throw runtime_error("Pinned QFork module image base changed");
            }
            pinnedModules.push_back(pinned);

            wstring canonicalPath;
            HANDLE pinnedFile = OpenQForkModuleFile(
                descriptors[i].path.c_str(), canonicalPath,
                descriptors[i].name.c_str());
            if (pinnedFile == NULL)
                throw runtime_error("Could not lock QFork module image file");
            pinnedModuleFiles.push_back(pinnedFile);

            QForkCapturedModule captured;
            captured.descriptor = descriptors[i];
            captured.descriptor.path = canonicalPath;
            captured.nameOffset = 0;
            captured.pathOffset = 0;
            captured.firstRange = static_cast<uint32_t>(totalRanges);
            if (!InspectQForkModuleImage(pinned, pinnedFile,
                    captured.descriptor.name.c_str(), captured.image))
                throw runtime_error("Could not validate QFork module image");
            if (!QForkCheckedAdd(totalRanges,
                    captured.image.writableRanges.size(), &totalRanges) ||
                totalRanges > numeric_limits<uint32_t>::max())
                throw runtime_error("Too many writable ranges in QFork snapshot");
            modules.push_back(captured);
        }

        uint64_t cursor;
        uint64_t bytes;
        if (!QForkCheckedAlign(sizeof(QForkModuleSnapshotHeader), 8, &cursor))
            throw runtime_error("QFork snapshot size overflow");
        uint64_t modulesOffset = cursor;
        if (!QForkCheckedMultiply(modules.size(),
                sizeof(QForkModuleSnapshotRecord), &bytes) ||
            !QForkCheckedAdd(cursor, bytes, &cursor) ||
            !QForkCheckedAlign(cursor, 8, &cursor))
            throw runtime_error("QFork snapshot module table overflow");
        uint64_t rangesOffset = cursor;
        if (!QForkCheckedMultiply(totalRanges,
                sizeof(QForkModuleSnapshotRange), &bytes) ||
            !QForkCheckedAdd(cursor, bytes, &cursor) ||
            !QForkCheckedAlign(cursor, 8, &cursor))
            throw runtime_error("QFork snapshot range table overflow");
        uint64_t payloadOffset = cursor;

        for (size_t i = 0; i < modules.size(); i++) {
            if (modules[i].descriptor.name.size() + 1 >
                    numeric_limits<uint32_t>::max() ||
                modules[i].descriptor.path.size() + 1 >
                    numeric_limits<uint32_t>::max())
                throw runtime_error("QFork module name or path is too long");

            modules[i].nameOffset = cursor;
            if (!QForkCheckedAdd(cursor,
                    modules[i].descriptor.name.size() + 1, &cursor) ||
                !QForkCheckedAlign(cursor, sizeof(wchar_t), &cursor))
                throw runtime_error("QFork snapshot name overflow");
            modules[i].pathOffset = cursor;
            if (!QForkCheckedMultiply(modules[i].descriptor.path.size() + 1,
                    sizeof(wchar_t), &bytes) ||
                !QForkCheckedAdd(cursor, bytes, &cursor) ||
                !QForkCheckedAlign(cursor, 8, &cursor))
                throw runtime_error("QFork snapshot path overflow");

            for (size_t j = 0; j < modules[i].image.writableRanges.size(); j++) {
                modules[i].image.writableRanges[j].snapshotOffset = cursor;
                if (!QForkCheckedAdd(cursor,
                        modules[i].image.writableRanges[j].size, &cursor) ||
                    !QForkCheckedAlign(cursor, 8, &cursor))
                    throw runtime_error("QFork snapshot data overflow");
            }
        }

        if (cursor > numeric_limits<SIZE_T>::max())
            throw runtime_error("QFork module snapshot is too large");

        DWORD sizeHigh = static_cast<DWORD>(cursor >> 32);
        DWORD sizeLow = static_cast<DWORD>(cursor & MAXDWORD);
        writableMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL,
            PAGE_READWRITE, sizeHigh, sizeLow, NULL);
        if (writableMap == NULL)
            throw system_error(GetLastError(), system_category(),
                "Could not create QFork module snapshot mapping");
        view = reinterpret_cast<byte *>(MapViewOfFile(writableMap,
            FILE_MAP_WRITE, 0, 0, static_cast<SIZE_T>(cursor)));
        if (view == NULL)
            throw system_error(GetLastError(), system_category(),
                "Could not map writable QFork module snapshot");
        memset(view, 0, static_cast<SIZE_T>(cursor));

        QForkModuleSnapshotHeader *header =
            reinterpret_cast<QForkModuleSnapshotHeader *>(view);
        header->magic = cQForkModuleSnapshotMagic;
        header->version = cQForkModuleSnapshotVersion;
        header->headerSize = sizeof(*header);
        header->moduleCount = static_cast<uint32_t>(modules.size());
        header->rangeCount = static_cast<uint32_t>(totalRanges);
        header->totalSize = cursor;
        header->modulesOffset = modulesOffset;
        header->rangesOffset = rangesOffset;
        header->payloadOffset = payloadOffset;

        QForkModuleSnapshotRecord *records =
            reinterpret_cast<QForkModuleSnapshotRecord *>(view + modulesOffset);
        QForkModuleSnapshotRange *ranges =
            reinterpret_cast<QForkModuleSnapshotRange *>(view + rangesOffset);
        uint32_t rangeIndex = 0;
        for (size_t i = 0; i < modules.size(); i++) {
            QForkModuleSnapshotRecord& record = records[i];
            record.sequence = modules[i].descriptor.sequence;
            record.expectedBase = reinterpret_cast<uint64_t>(
                modules[i].descriptor.expectedBase);
            record.nameOffset = modules[i].nameOffset;
            record.pathOffset = modules[i].pathOffset;
            record.nameBytes = static_cast<uint32_t>(
                modules[i].descriptor.name.size() + 1);
            record.pathChars = static_cast<uint32_t>(
                modules[i].descriptor.path.size() + 1);
            record.firstRange = modules[i].firstRange;
            record.rangeCount = static_cast<uint32_t>(
                modules[i].image.writableRanges.size());
            record.imageSize = modules[i].image.imageSize;
            record.sizeOfHeaders = modules[i].image.sizeOfHeaders;
            record.timeDateStamp = modules[i].image.timeDateStamp;
            record.checkSum = modules[i].image.checkSum;
            record.machine = modules[i].image.machine;
            record.optionalMagic = modules[i].image.optionalMagic;
            record.numberOfSections = modules[i].image.numberOfSections;
            record.volumeSerialNumber =
                modules[i].image.fileIdentity.volumeSerialNumber;
            record.fileIndexHigh = modules[i].image.fileIdentity.fileIndexHigh;
            record.fileIndexLow = modules[i].image.fileIdentity.fileIndexLow;
            record.fileSize = modules[i].image.fileIdentity.fileSize;
            record.lastWriteTime = modules[i].image.fileIdentity.lastWriteTime;

            memcpy(view + record.nameOffset,
                   modules[i].descriptor.name.c_str(), record.nameBytes);
            memcpy(view + record.pathOffset,
                   modules[i].descriptor.path.c_str(),
                   static_cast<size_t>(record.pathChars) * sizeof(wchar_t));

            for (size_t j = 0; j < modules[i].image.writableRanges.size(); j++) {
                QForkModuleImageRange& source =
                    modules[i].image.writableRanges[j];
                QForkModuleSnapshotRange& range = ranges[rangeIndex++];
                range.bytesOffset = source.snapshotOffset;
                range.rva = source.rva;
                range.size = source.size;
                range.characteristics = source.characteristics;
                range.sectionIndex = source.sectionIndex;
                memcpy(range.name, source.name, sizeof(range.name));
                memcpy(view + range.bytesOffset, source.address, range.size);
            }
        }

        if (!UnmapViewOfFile(view))
            throw system_error(GetLastError(), system_category(),
                "Could not unmap writable QFork module snapshot");
        view = NULL;

        if (!DuplicateHandle(GetCurrentProcess(), writableMap,
                GetCurrentProcess(), &readOnlyMap,
                SECTION_MAP_READ | SECTION_QUERY, FALSE, 0))
            throw system_error(GetLastError(), system_category(),
                "Could not create read-only QFork module snapshot handle");
        CloseHandle(writableMap);
        writableMap = NULL;

        g_hQForkModuleSnapshotMap = readOnlyMap;
        readOnlyMap = NULL;
        g_QForkPinnedModules.swap(pinnedModules);
        g_QForkPinnedModuleFiles.swap(pinnedModuleFiles);
        g_QForkModuleSnapshotSize = cursor;
        g_QForkModuleSnapshotCount = static_cast<uint32_t>(modules.size());
        serverLog(LL_DEBUG,
            "QFork captured %u module images in %llu bytes",
            static_cast<unsigned>(modules.size()),
            static_cast<unsigned long long>(cursor));
    }
    catch (...) {
        if (view != NULL) UnmapViewOfFile(view);
        if (readOnlyMap != NULL) CloseHandle(readOnlyMap);
        if (writableMap != NULL) CloseHandle(writableMap);
        ReleasePinnedQForkModuleFiles(pinnedModuleFiles);
        ReleasePinnedQForkModules(pinnedModules);
        throw;
    }
}

static const byte *QForkSnapshotSpan(const byte *snapshot, size_t snapshotSize,
    uint64_t offset, uint64_t size, size_t alignment)
{
    if (alignment != 0 && offset % alignment != 0) return NULL;
    if (offset > snapshotSize || size > snapshotSize - offset) return NULL;
    return snapshot + offset;
}

static bool ValidateQForkModuleSnapshot(const byte *snapshot,
    size_t snapshotSize, uint32_t expectedCount,
    const QForkModuleSnapshotHeader **headerOut,
    const QForkModuleSnapshotRecord **recordsOut,
    const QForkModuleSnapshotRange **rangesOut)
{
    if (snapshot == NULL ||
        snapshotSize < sizeof(QForkModuleSnapshotHeader))
        return false;

    const QForkModuleSnapshotHeader *header =
        reinterpret_cast<const QForkModuleSnapshotHeader *>(snapshot);
    if (header->magic != cQForkModuleSnapshotMagic ||
        header->version != cQForkModuleSnapshotVersion ||
        header->headerSize != sizeof(*header) ||
        header->moduleCount != expectedCount ||
        header->totalSize != snapshotSize)
        return false;

    uint64_t moduleBytes;
    uint64_t rangeBytes;
    uint64_t modulesOffset;
    uint64_t modulesEnd;
    uint64_t rangesOffset;
    uint64_t rangesEnd;
    uint64_t payloadOffset;
    if (!QForkCheckedMultiply(header->moduleCount,
            sizeof(QForkModuleSnapshotRecord), &moduleBytes) ||
        !QForkCheckedMultiply(header->rangeCount,
            sizeof(QForkModuleSnapshotRange), &rangeBytes) ||
        !QForkCheckedAlign(sizeof(QForkModuleSnapshotHeader), 8,
            &modulesOffset) ||
        !QForkCheckedAdd(modulesOffset, moduleBytes, &modulesEnd) ||
        !QForkCheckedAlign(modulesEnd, 8, &rangesOffset) ||
        !QForkCheckedAdd(rangesOffset, rangeBytes, &rangesEnd) ||
        !QForkCheckedAlign(rangesEnd, 8, &payloadOffset) ||
        header->modulesOffset != modulesOffset ||
        header->rangesOffset != rangesOffset ||
        header->payloadOffset != payloadOffset)
        return false;

    const byte *moduleTable = QForkSnapshotSpan(snapshot, snapshotSize,
        header->modulesOffset, moduleBytes, 8);
    const byte *rangeTable = QForkSnapshotSpan(snapshot, snapshotSize,
        header->rangesOffset, rangeBytes, 8);
    if (moduleTable == NULL || rangeTable == NULL ||
        header->payloadOffset > snapshotSize)
        return false;

    const QForkModuleSnapshotRecord *records =
        reinterpret_cast<const QForkModuleSnapshotRecord *>(moduleTable);
    const QForkModuleSnapshotRange *ranges =
        reinterpret_cast<const QForkModuleSnapshotRange *>(rangeTable);
    uint64_t previousSequence = 0;
    uint64_t payloadCursor = header->payloadOffset;
    uint32_t nextRange = 0;
    for (uint32_t i = 0; i < header->moduleCount; i++) {
        const QForkModuleSnapshotRecord& record = records[i];
        if (record.nameBytes == 0 || record.pathChars == 0 ||
            record.expectedBase == 0 ||
            (i != 0 && record.sequence <= previousSequence) ||
            record.firstRange != nextRange ||
            record.rangeCount > header->rangeCount - nextRange ||
            record.imageSize == 0 || record.sizeOfHeaders == 0 ||
            record.sizeOfHeaders > record.imageSize ||
            record.machine != IMAGE_FILE_MACHINE_AMD64 ||
            record.optionalMagic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            record.numberOfSections == 0 || record.reserved != 0 ||
            record.fileIdentityReserved != 0 ||
            record.expectedBase >
                numeric_limits<uint64_t>::max() - record.imageSize)
            return false;
        previousSequence = record.sequence;

        if (record.nameOffset != payloadCursor) return false;
        const char *name = reinterpret_cast<const char *>(
            QForkSnapshotSpan(snapshot, snapshotSize, record.nameOffset,
                record.nameBytes, 1));
        uint64_t nameEnd;
        uint64_t pathBytes;
        if (!QForkCheckedAdd(record.nameOffset, record.nameBytes, &nameEnd) ||
            !QForkCheckedAlign(nameEnd, sizeof(wchar_t), &payloadCursor) ||
            record.pathOffset != payloadCursor ||
            !QForkCheckedMultiply(record.pathChars, sizeof(wchar_t),
                                  &pathBytes))
            return false;
        const wchar_t *path = reinterpret_cast<const wchar_t *>(
            QForkSnapshotSpan(snapshot, snapshotSize, record.pathOffset,
                pathBytes, sizeof(wchar_t)));
        uint64_t pathEnd;
        if (name == NULL || path == NULL ||
            name[record.nameBytes - 1] != '\0' ||
            path[record.pathChars - 1] != L'\0' ||
            memchr(name, '\0', record.nameBytes - 1) != NULL ||
            wmemchr(path, L'\0', record.pathChars - 1) != NULL ||
            !QForkCheckedAdd(record.pathOffset, pathBytes, &pathEnd) ||
            !QForkCheckedAlign(pathEnd, 8, &payloadCursor))
            return false;

        uint64_t previousEnd = 0;
        for (uint32_t j = 0; j < record.rangeCount; j++) {
            const QForkModuleSnapshotRange& range =
                ranges[record.firstRange + j];
            if (range.size == 0 ||
                range.bytesOffset != payloadCursor ||
                range.reserved != 0 ||
                range.sectionIndex >= record.numberOfSections ||
                !(range.characteristics & IMAGE_SCN_MEM_WRITE) ||
                (range.characteristics & (IMAGE_SCN_MEM_EXECUTE |
                    IMAGE_SCN_MEM_DISCARDABLE | IMAGE_SCN_MEM_SHARED)) ||
                QForkSnapshotSpan(snapshot, snapshotSize, range.bytesOffset,
                    range.size, 1) == NULL ||
                !QForkModuleRangeInsideImage(range.rva, range.size,
                                              record.imageSize) ||
                (j != 0 && range.rva < previousEnd))
                return false;
            previousEnd = static_cast<uint64_t>(range.rva) + range.size;
            uint64_t rangeEnd;
            if (!QForkCheckedAdd(range.bytesOffset, range.size, &rangeEnd) ||
                !QForkCheckedAlign(rangeEnd, 8, &payloadCursor))
                return false;
        }
        nextRange += record.rangeCount;
    }

    if (nextRange != header->rangeCount || payloadCursor != header->totalSize)
        return false;

    *headerOut = header;
    *recordsOut = records;
    *rangesOut = ranges;
    return true;
}

static bool RestoreQForkModuleImage(const byte *snapshot, size_t snapshotSize,
    const QForkModuleSnapshotRecord& record,
    const QForkModuleSnapshotRange *ranges)
{
    const char *name = reinterpret_cast<const char *>(snapshot +
                                                       record.nameOffset);
    const wchar_t *path = reinterpret_cast<const wchar_t *>(snapshot +
                                                             record.pathOffset);

    QForkModuleFileIdentity recordedIdentity;
    recordedIdentity.volumeSerialNumber = record.volumeSerialNumber;
    recordedIdentity.fileIndexHigh = record.fileIndexHigh;
    recordedIdentity.fileIndexLow = record.fileIndexLow;
    recordedIdentity.fileSize = record.fileSize;
    recordedIdentity.lastWriteTime = record.lastWriteTime;

    wstring canonicalPath;
    HANDLE childModuleFile = OpenQForkModuleFile(path, canonicalPath, name);
    if (childModuleFile == NULL) return false;
    CloseHandleGuard pinnedFile(childModuleFile);
    QForkModuleFileIdentity childFileIdentity;
    if (canonicalPath != path ||
        !GetQForkModuleFileIdentity(childModuleFile, childFileIdentity, name) ||
        !QForkModuleFileIdentityEqual(childFileIdentity, recordedIdentity))
    {
        serverLog(LL_WARNING,
            "QFork module restore: locked image changed for %s", name);
        return false;
    }

    HMODULE childBase = LoadLibraryExW(path, NULL,
                                      LOAD_WITH_ALTERED_SEARCH_PATH);
    if (childBase == NULL) {
        serverLog(LL_WARNING,
            "QFork module restore: LoadLibrary failed for %s (0x%08x)",
            name, GetLastError());
        return false;
    }
    if (reinterpret_cast<uint64_t>(childBase) != record.expectedBase) {
        serverLog(LL_WARNING,
            "QFork module restore: %s loaded at %p, expected %p",
            name, childBase, reinterpret_cast<void *>(record.expectedBase));
        return false;
    }

    QForkModuleImageInfo image;
    if (!InspectQForkModuleImage(childBase, childModuleFile, name, image))
        return false;
    if (image.imageSize != record.imageSize ||
        image.sizeOfHeaders != record.sizeOfHeaders ||
        image.timeDateStamp != record.timeDateStamp ||
        image.checkSum != record.checkSum ||
        image.machine != record.machine ||
        image.optionalMagic != record.optionalMagic ||
        image.numberOfSections != record.numberOfSections ||
        !QForkModuleFileIdentityEqual(image.fileIdentity, recordedIdentity) ||
        image.writableRanges.size() != record.rangeCount)
    {
        serverLog(LL_WARNING,
            "QFork module restore: image metadata changed for %s", name);
        return false;
    }

    byte *imageBase = reinterpret_cast<byte *>(childBase);
    for (uint32_t i = 0; i < record.rangeCount; i++) {
        const QForkModuleSnapshotRange& source = ranges[record.firstRange + i];
        const QForkModuleImageRange& target = image.writableRanges[i];
        if (target.rva != source.rva || target.size != source.size ||
            target.characteristics != source.characteristics ||
            target.sectionIndex != source.sectionIndex ||
            memcmp(target.name, source.name, sizeof(source.name)) != 0)
        {
            serverLog(LL_WARNING,
                "QFork module restore: writable section layout changed for %s",
                name);
            return false;
        }

        const byte *parentBytes = QForkSnapshotSpan(snapshot, snapshotSize,
            source.bytesOffset, source.size, 1);
        if (parentBytes == NULL) return false;
        vector<byte> childBytes(source.size);
        memcpy(childBytes.data(), target.address, source.size);

        DWORD oldProtect;
        if (!VirtualProtect(target.address, source.size, PAGE_READWRITE,
                            &oldProtect))
        {
            serverLog(LL_WARNING,
                "QFork module restore: could not unprotect %s state (0x%08x)",
                name, GetLastError());
            return false;
        }

        memcpy(target.address, parentBytes, source.size);
        RestoreQForkModuleProtectedRanges(imageBase, target.rva, childBytes,
                                          image.protectedRanges);

        DWORD ignored;
        if (!VirtualProtect(target.address, source.size, oldProtect, &ignored)) {
            serverLog(LL_WARNING,
                "QFork module restore: could not reprotect %s state (0x%08x)",
                name, GetLastError());
            return false;
        }
    }

    serverLog(LL_DEBUG, "QFork restored module %s at %p",
              name, childBase);
    return true;
}

static bool RestoreQForkModules(const byte *snapshot, size_t snapshotSize,
    uint32_t expectedCount)
{
    if (expectedCount == 0)
        return snapshot == NULL && snapshotSize == 0;

    const QForkModuleSnapshotHeader *header;
    const QForkModuleSnapshotRecord *records;
    const QForkModuleSnapshotRange *ranges;
    if (!ValidateQForkModuleSnapshot(snapshot, snapshotSize, expectedCount,
                                     &header, &records, &ranges))
        return false;

    for (uint32_t i = 0; i < header->moduleCount; i++) {
        if (!RestoreQForkModuleImage(snapshot, snapshotSize, records[i], ranges))
            return false;
    }
    return true;
}

extern "C" int QForkPrepareModuleSnapshot(void)
{
    try {
        PrepareQForkModuleSnapshot();
        return 0;
    } catch (const exception& ex) {
        serverLog(LL_WARNING, "QFork module snapshot failed: %s", ex.what());
        ReleaseQForkModuleSnapshot();
        return -1;
    } catch (...) {
        serverLog(LL_WARNING, "QFork module snapshot failed");
        ReleaseQForkModuleSnapshot();
        return -1;
    }
}

extern "C" void QForkReleaseModuleSnapshot(void)
{
    ReleaseQForkModuleSnapshot();
}

extern "C" void *QForkGetModuleSnapshotHandle(void)
{
    return (void *)g_hQForkModuleSnapshotMap;
}

extern "C" uint64_t QForkGetModuleSnapshotSize(void)
{
    return g_QForkModuleSnapshotSize;
}

extern "C" uint32_t QForkGetModuleSnapshotCount(void)
{
    return g_QForkModuleSnapshotCount;
}

extern "C" int QForkRestoreModuleSnapshot(void *parent_process,
                                          void *parent_snapshot_handle,
                                          uint64_t size, uint32_t count)
{
    HANDLE parent = (HANDLE)parent_process;
    HANDLE local = NULL;
    const byte *view = NULL;
    int rc = -1;

    if (count == 0) {
        if (parent_snapshot_handle != NULL || size != 0) {
            serverLog(LL_WARNING,
                      "QFork module restore: empty snapshot metadata is inconsistent");
            return -1;
        }
        return 0;
    }
    if (parent == NULL || parent_snapshot_handle == NULL ||
        size < sizeof(QForkModuleSnapshotHeader))
    {
        serverLog(LL_WARNING, "QFork module restore: invalid snapshot handle");
        return -1;
    }
    if (!DuplicateHandle(parent, (HANDLE)parent_snapshot_handle,
                         GetCurrentProcess(), &local, 0, FALSE,
                         DUPLICATE_SAME_ACCESS))
    {
        serverLog(LL_WARNING,
                  "QFork module restore: DuplicateHandle gle=%lu",
                  GetLastError());
        return -1;
    }
    view = reinterpret_cast<const byte *>(
        MapViewOfFile(local, FILE_MAP_READ, 0, 0, (SIZE_T)size));
    if (view == NULL) {
        serverLog(LL_WARNING,
                  "QFork module restore: MapViewOfFile gle=%lu",
                  GetLastError());
        CloseHandle(local);
        return -1;
    }
    if (RestoreQForkModules(view, (size_t)size, count))
        rc = 0;
    else
        serverLog(LL_WARNING, "QFork module restore: image restore failed");
    UnmapViewOfFile(view);
    CloseHandle(local);
    return rc;
}

