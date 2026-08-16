/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/*
 * Windows Event Log. IDs 0x0-0x3 are frozen (EventLog.mc).
 * Lifted from tporadowski 5.0.14 Win32_EventLog.cpp (MSOpenTech BSD-3-Clause).
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <string>

#include "Win32_EventLog.h"
#include "Win32_Service.h"
#include "EventLog.h"
#include <system_error>

static bool g_eventLogEnabled = true;
static std::string g_eventLogIdentity = "redis";

static const char *kEventLogPath = "SYSTEM\\CurrentControlSet\\Services\\EventLog\\";
static const char *kEventLogAppPath = "SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\";
static const char *kRedis = "redis";
static const char *kRedisServer = "redis-server";
static const char *kEventMessageFile = "EventMessageFile";
static const char *kTypesSupported = "TypesSupported";
static const char *kApplication = "Application";

static const DWORD kTypesSupportedValue =
    EVENTLOG_ERROR_TYPE | EVENTLOG_WARNING_TYPE |
    EVENTLOG_INFORMATION_TYPE | EVENTLOG_SUCCESS;

struct RegKey {
    HKEY h;
    RegKey() : h(NULL) {}
    ~RegKey() {
        if (h)
            RegCloseKey(h);
    }
    operator HKEY() const { return h; }
    operator HKEY *() { return &h; }
};

static void log_message(const char *msg, WORD type) {
    DWORD eventID;
    switch (type) {
    case EVENTLOG_ERROR_TYPE:
        eventID = MSG_ERROR_1;
        break;
    case EVENTLOG_WARNING_TYPE:
        eventID = MSG_WARNING_1;
        break;
    case EVENTLOG_SUCCESS:
        eventID = MSG_SUCCESS_1;
        break;
    default:
        eventID = MSG_INFO_1;
        break;
    }

    HANDLE hEventLog = RegisterEventSourceA(NULL, kRedis);
    if (!hEventLog) {
        fprintf(stderr, "EventLog: RegisterEventSource failed gle=%lu\n",
                GetLastError());
        return;
    }
    if (!ReportEventA(hEventLog, type, 0, eventID, NULL, 1, 0, &msg, NULL)) {
        fprintf(stderr, "EventLog: ReportEvent failed gle=%lu\n", GetLastError());
    }
    DeregisterEventSource(hEventLog);
}

static std::string prefixed(const char *msg) {
    std::string out = "syslog-ident = ";
    out += g_eventLogIdentity;
    out += "\n";
    if (msg)
        out += msg;
    return out;
}

void setSyslogEnabled(int enabled) {
    g_eventLogEnabled = (enabled != 0);
}

void setSyslogIdent(const char *identity) {
    if (identity && identity[0])
        g_eventLogIdentity = identity;
}

int IsEventLogEnabled(void) {
    return g_eventLogEnabled ? 1 : 0;
}

void WriteEventLog(const char *msg) {
    try {
        std::string s = prefixed(msg);
        log_message(s.c_str(), EVENTLOG_INFORMATION_TYPE);
    } catch (...) {
    }
}

void WriteEventLogError(const char *msg) {
    try {
        std::string s = prefixed(msg);
        log_message(s.c_str(), EVENTLOG_ERROR_TYPE);
    } catch (...) {
    }
}

void WriteEventLogSuccess(const char *msg) {
    try {
        std::string s = prefixed(msg);
        log_message(s.c_str(), EVENTLOG_SUCCESS);
    } catch (...) {
    }
}

static LONG set_sz_if_missing(HKEY key, const char *name, const char *value) {
    DWORD type = 0, size = 0;
    LONG rc = RegQueryValueExA(key, name, NULL, &type, NULL, &size);
    if (rc == ERROR_SUCCESS)
        return ERROR_SUCCESS;
    return RegSetValueExA(key, name, 0, REG_SZ, (const BYTE *)value,
                          (DWORD)strlen(value) + 1);
}

static LONG set_dword_if_missing(HKEY key, const char *name, DWORD value) {
    DWORD type = 0, size = 0;
    LONG rc = RegQueryValueExA(key, name, NULL, &type, NULL, &size);
    if (rc == ERROR_SUCCESS)
        return ERROR_SUCCESS;
    return RegSetValueExA(key, name, 0, REG_DWORD, (const BYTE *)&value,
                          sizeof(DWORD));
}

static LONG open_or_create(HKEY parent, const char *name, HKEY *out) {
    LONG rc = RegOpenKeyA(parent, name, out);
    if (rc == ERROR_SUCCESS)
        return rc;
    return RegCreateKeyA(parent, name, out);
}

void InstallEventLogSource(const char *appPath) {
    if (!appPath)
        return;

    RegKey eventLogKey;
    if (RegOpenKeyA(HKEY_LOCAL_MACHINE, kEventLogPath, eventLogKey) != ERROR_SUCCESS)
        throw std::system_error(GetLastError(), std::system_category(),
                                "RegOpenKey EventLog failed");

    RegKey redisKey;
    if (open_or_create(eventLogKey, kRedis, redisKey) != ERROR_SUCCESS)
        throw std::system_error(GetLastError(), std::system_category(),
                                "RegCreateKey redis failed");

    RegKey serverKey;
    if (open_or_create(redisKey, kRedisServer, serverKey) != ERROR_SUCCESS)
        throw std::system_error(GetLastError(), std::system_category(),
                                "RegCreateKey redis-server failed");

    if (set_dword_if_missing(serverKey, kTypesSupported, kTypesSupportedValue) != ERROR_SUCCESS ||
        set_sz_if_missing(serverKey, kEventMessageFile, appPath) != ERROR_SUCCESS)
        throw std::system_error(GetLastError(), std::system_category(),
                                "RegSetValueEx EventLog source failed");

    RegKey application;
    if (RegOpenKeyA(eventLogKey, kApplication, application) != ERROR_SUCCESS)
        throw std::system_error(GetLastError(), std::system_category(),
                                "RegOpenKey Application failed");

    RegKey appRedis;
    if (open_or_create(application, kRedis, appRedis) != ERROR_SUCCESS)
        throw std::system_error(GetLastError(), std::system_category(),
                                "RegCreateKey Application\\redis failed");

    if (set_dword_if_missing(appRedis, kTypesSupported, kTypesSupportedValue) != ERROR_SUCCESS ||
        set_sz_if_missing(appRedis, kEventMessageFile, appPath) != ERROR_SUCCESS)
        throw std::system_error(GetLastError(), std::system_category(),
                                "RegSetValueEx Application\\redis failed");
}

void UninstallEventLogSource(void) {
    RegKey appKey;
    if (RegOpenKeyA(HKEY_LOCAL_MACHINE, kEventLogAppPath, appKey) == ERROR_SUCCESS)
        RegDeleteKeyA(appKey, kRedis);

    RegKey eventLogKey;
    if (RegOpenKeyA(HKEY_LOCAL_MACHINE, kEventLogPath, eventLogKey) != ERROR_SUCCESS)
        return;
    RegKey redisKey;
    if (RegOpenKeyA(eventLogKey, kRedis, redisKey) != ERROR_SUCCESS)
        return;
    RegDeleteKeyA(redisKey, kRedisServer);
    RegDeleteKeyA(eventLogKey, kRedis);
}

static HANDLE g_serviceStopEvent = NULL;
static int g_runningAsService = 0;
static char g_serviceNameBuf[257] = "Redis";

void Win32ServiceSetStopEvent(void *handle) {
    g_serviceStopEvent = (HANDLE)handle;
}

void Win32ServiceSetRunning(int running) {
    g_runningAsService = running ? 1 : 0;
}

void Win32ServiceSetName(const char *name) {
    if (!name || !name[0])
        return;
    strncpy(g_serviceNameBuf, name, sizeof(g_serviceNameBuf) - 1);
    g_serviceNameBuf[sizeof(g_serviceNameBuf) - 1] = '\0';
}

int ServiceStopIssued(void) {
    if (!g_serviceStopEvent || g_serviceStopEvent == INVALID_HANDLE_VALUE)
        return 0;
    return WaitForSingleObject(g_serviceStopEvent, 0) == WAIT_OBJECT_0 ? 1 : 0;
}

int RunningAsService(void) {
    return g_runningAsService;
}

const char *GetServiceName(void) {
    return g_serviceNameBuf;
}

#ifdef __cplusplus
extern "C" {
#endif

void openlog(const char *ident, int option, int facility) {
    (void)option;
    (void)facility;
    setSyslogIdent(ident);
}

void closelog(void) {}

void syslog(int priority, const char *format, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, format);
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    if (priority <= 3)
        WriteEventLogError(buf);
    else
        WriteEventLog(buf);
}

int setlogmask(int mask) {
    return mask;
}

#ifdef __cplusplus
}
#endif
