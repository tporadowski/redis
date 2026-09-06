/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_STACKTRACE_H
#define WIN32_STACKTRACE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Install SetUnhandledExceptionFilter + SIGABRT. Idempotent. */
void StackTraceInit(void);
/* Restore the previous unhandled-exception filter and SIGABRT. */
void StackTraceShutdown(void);
/* Walk and log the current thread, or the crash CONTEXT if non-NULL. */
void win32_log_stack_trace(const void *context);

#ifdef __cplusplus
}
#endif

#endif
