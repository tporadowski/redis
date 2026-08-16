/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/*
 * Windows Service CLI (--service-install/uninstall/start/stop/name/run).
 * Lifted from tporadowski 5.0.14 Win32_Service.h (MSOpenTech BSD-3-Clause).
 */
#ifndef WIN32_INTEROP_SERVICE_H
#define WIN32_INTEROP_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

int RunningAsService(void);
const char *GetServiceName(void);
/* 1 = a --service-* command was handled (caller should exit 0). */
int HandleServiceCommands(int argc, char **argv);
int ServiceStopIssued(void);
/* SCM worker publishes the stop event so aeProcessEvents can aeStop. */
void Win32ServiceSetStopEvent(void *handle);
void Win32ServiceSetRunning(int running);
void Win32ServiceSetName(const char *name);

#ifdef __cplusplus
}
#endif

#endif
