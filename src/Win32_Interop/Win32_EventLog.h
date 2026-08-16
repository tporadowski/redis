/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/*
 * Windows Event Log (IDs 0x0-0x3 from EventLog.mc).
 * Lifted from tporadowski 5.0.14 Win32_EventLog.h (MSOpenTech BSD-3-Clause).
 */
#ifndef WIN32_INTEROP_EVENTLOG_H
#define WIN32_INTEROP_EVENTLOG_H

#ifdef __cplusplus
extern "C" {
#endif

void setSyslogEnabled(int enabled);
void setSyslogIdent(const char *identity);
int IsEventLogEnabled(void);
void WriteEventLog(const char *msg);
void WriteEventLogError(const char *msg);
void WriteEventLogSuccess(const char *msg);

void InstallEventLogSource(const char *appPath);
void UninstallEventLogSource(void);

#ifdef __cplusplus
}
#endif

#endif
