/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/*
 * Windows Service: --service-install / --service-uninstall / --service-start
 * / --service-stop / --service-name / --service-run.
 *
 * Lifted from tporadowski 5.0.14 Win32_service.cpp (MSOpenTech BSD-3-Clause).
 * No Win32_CommandLine / SmartHandle / RedisLog: argv is scanned here.
 * HandleServiceCommands runs before QForkParentInit. The SCM worker calls
 * RedisWindowsParentMain so the service process still sets up the QFork heap.
 *
 * --service-install must be argv[1]. Extra args become the service ImagePath
 * (with --service-install rewritten to --service-run). Autostart as
 * NT AUTHORITY\NetworkService. Does not start the service.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <aclapi.h>
#include <accctrl.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sstream>
#include <vector>
#include <system_error>

#include "Win32_Service.h"
#include "Win32_EventLog.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

#ifdef __cplusplus
extern "C"
#endif
int RedisWindowsParentMain(int argc, char **argv);

#define DEFAULT_SERVICE_NAME "Redis"
#define MAX_SERVICE_NAME_LENGTH 256

static char g_serviceName[MAX_SERVICE_NAME_LENGTH + 1] = DEFAULT_SERVICE_NAME;
static SERVICE_STATUS g_ServiceStatus = {0};
static HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;
static HANDLE g_ServiceStoppedEvent = INVALID_HANDLE_VALUE;
static std::vector<std::string> serviceRunArguments;
static SERVICE_STATUS_HANDLE g_StatusHandle;
static const ULONGLONG cThirtySeconds = 30 * 1000;
static int g_isRunningAsService = 0;
static const int cPreshutdownInterval = 180000;
static const char *cServiceInstallPipeName = "\\\\.\\pipe\\redis-service-install";

struct ScHandle {
    SC_HANDLE h;
    ScHandle() : h(NULL) {}
    ScHandle &operator=(SC_HANDLE x) {
        close();
        h = x;
        return *this;
    }
    bool invalid() const { return h == NULL; }
    bool valid() const { return h != NULL; }
    operator SC_HANDLE() const { return h; }
    void close() {
        if (h) {
            CloseServiceHandle(h);
            h = NULL;
        }
    }
    ~ScHandle() { close(); }
};

static void service_print(const std::string &message) {
    HANDLE pipe = CreateFileA(cServiceInstallPipeName, GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (pipe != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(pipe, message.c_str(), (DWORD)message.size(), &written, NULL);
        CloseHandle(pipe);
    }
    fprintf(stderr, "%s\n", message.c_str());
}

static void apply_service_name(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (_stricmp(argv[i], "--service-name") == 0 && i + 1 < argc) {
            if (strlen(argv[i + 1]) > MAX_SERVICE_NAME_LENGTH)
                throw std::runtime_error("Service name too long.");
            strncpy(g_serviceName, argv[i + 1], MAX_SERVICE_NAME_LENGTH);
            g_serviceName[MAX_SERVICE_NAME_LENGTH] = '\0';
            Win32ServiceSetName(g_serviceName);
            return;
        }
    }
}

static std::string quote_arg(const char *arg) {
    if (!arg)
        return "";
    if (strchr(arg, ' ')) {
        std::string s = "\"";
        s += arg;
        s += "\"";
        return s;
    }
    return arg;
}

static std::string dir_of(const std::string &path) {
    std::string p = path;
    for (char &c : p) {
        if (c == '/')
            c = '\\';
    }
    size_t slash = p.find_last_of('\\');
    if (slash == std::string::npos)
        return ".";
    if (slash == 0)
        return "\\";
    return p.substr(0, slash);
}

static std::string exe_path() {
    char sz[MAX_PATH];
    if (!GetModuleFileNameA(NULL, sz, MAX_PATH))
        throw std::system_error(GetLastError(), std::system_category(),
                                "GetModuleFileNameA failed");
    return sz;
}

static std::string exe_dir() {
    return dir_of(exe_path());
}

static std::string abs_path(const char *p) {
    if (!p || !p[0])
        return "";
    if ((p[0] == '\\' && p[1] == '\\') || (p[0] && p[1] == ':'))
        return p;
    char cwd[MAX_PATH];
    if (!GetCurrentDirectoryA(MAX_PATH, cwd))
        return p;
    std::string out = cwd;
    out += '\\';
    out += p;
    return out;
}

static std::vector<std::string> access_paths(int argc, char **argv) {
    std::vector<std::string> paths;
    paths.push_back(exe_dir());

    const char *dir = NULL;
    const char *logfile = NULL;
    const char *conf = NULL;
    for (int i = 1; i < argc; i++) {
        if (_stricmp(argv[i], "--dir") == 0 && i + 1 < argc) {
            dir = argv[++i];
        } else if (_stricmp(argv[i], "--logfile") == 0 && i + 1 < argc) {
            logfile = argv[++i];
        } else if (_strnicmp(argv[i], "--service-", 10) == 0) {
            if (_stricmp(argv[i], "--service-name") == 0 && i + 1 < argc)
                i++;
        } else if (argv[i][0] != '-' && !conf) {
            conf = argv[i];
        }
    }
    if (dir && dir[0])
        paths.push_back(abs_path(dir));
    if (conf && conf[0])
        paths.push_back(dir_of(abs_path(conf)));
    if (logfile && logfile[0] && _stricmp(logfile, "stdout") != 0)
        paths.push_back(dir_of(abs_path(logfile)));
    return paths;
}

static DWORD add_ace(const char *path, const char *trustee) {
    PACL oldDacl = NULL, newDacl = NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    EXPLICIT_ACCESSA ea;
    DWORD rc = GetNamedSecurityInfoA((LPSTR)path, SE_FILE_OBJECT,
                                     DACL_SECURITY_INFORMATION, NULL, NULL,
                                     &oldDacl, NULL, &sd);
    if (rc != ERROR_SUCCESS)
        return rc;

    ZeroMemory(&ea, sizeof(ea));
    ea.grfAccessPermissions = GENERIC_ALL;
    ea.grfAccessMode = GRANT_ACCESS;
    ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
    ea.Trustee.ptstrName = (LPSTR)trustee;

    rc = SetEntriesInAclA(1, &ea, oldDacl, &newDacl);
    if (rc == ERROR_SUCCESS) {
        rc = SetNamedSecurityInfoA((LPSTR)path, SE_FILE_OBJECT,
                                   DACL_SECURITY_INFORMATION, NULL, NULL,
                                   newDacl, NULL);
    }
    if (sd)
        LocalFree(sd);
    if (newDacl)
        LocalFree(newDacl);
    return rc;
}

static bool is_process_elevated() {
    HANDLE token = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        throw std::system_error(GetLastError(), std::system_category(),
                                "OpenProcessToken failed");
    TOKEN_ELEVATION elevation;
    DWORD size = 0;
    BOOL ok = GetTokenInformation(token, TokenElevation, &elevation,
                                  sizeof(elevation), &size);
    DWORD gle = GetLastError();
    CloseHandle(token);
    if (!ok)
        throw std::system_error(gle, std::system_category(),
                                "GetTokenInformation failed");
    return elevation.TokenIsElevated != 0;
}

static int relaunch_elevated(int argc, char **argv) {
    HANDLE pipe = CreateNamedPipeA(cServiceInstallPipeName, PIPE_ACCESS_INBOUND,
                                   PIPE_TYPE_BYTE | PIPE_NOWAIT, 1, 0, 0, 0,
                                   NULL);

    std::ostringstream params;
    for (int n = 1; n < argc; n++) {
        if (n > 1)
            params << ' ';
        params << quote_arg(argv[n]);
    }
    std::string paramStr = params.str();

    SHELLEXECUTEINFOA sei;
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.lpVerb = "runas";
    sei.lpFile = _pgmptr;
    sei.lpParameters = paramStr.c_str();
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    if (!ShellExecuteExA(&sei)) {
        if (pipe != INVALID_HANDLE_VALUE)
            CloseHandle(pipe);
        throw std::system_error(GetLastError(), std::system_category(),
                                "ShellExecuteExA failed");
    }
    if (sei.hProcess) {
        char buffer[10001];
        while (WaitForSingleObject(sei.hProcess, 50) != WAIT_OBJECT_0) {
            if (pipe != INVALID_HANDLE_VALUE) {
                DWORD bytesRead = 0;
                if (ReadFile(pipe, buffer, 10000, &bytesRead, NULL) &&
                    bytesRead > 0) {
                    buffer[bytesRead] = '\0';
                    fprintf(stderr, "%s", buffer);
                    if (buffer[bytesRead - 1] != '\n')
                        fprintf(stderr, "\n");
                }
            }
        }
        CloseHandle(sei.hProcess);
    }
    if (pipe != INVALID_HANDLE_VALUE)
        CloseHandle(pipe);
    return 1;
}

static void service_install(int argc, char **argv) {
    apply_service_name(argc, argv);
    std::string path = exe_path();
    const char *userName = "NT AUTHORITY\\NetworkService";

    std::ostringstream args;
    for (int a = 0; a < argc; a++) {
        if (a == 0) {
            args << '"' << path << '"';
        } else {
            args << ' ';
            if (a == 1)
                args << "--service-run";
            else
                args << quote_arg(argv[a]);
        }
    }

    ScHandle scm;
    scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (scm.invalid())
        throw std::system_error(GetLastError(), std::system_category(),
                                "OpenSCManager failed");

    ScHandle svc;
    svc = CreateServiceA(scm, g_serviceName, g_serviceName, SERVICE_ALL_ACCESS,
                         SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
                         SERVICE_ERROR_NORMAL, args.str().c_str(), NULL, NULL,
                         NULL, userName, NULL);
    if (svc.invalid())
        throw std::system_error(GetLastError(), std::system_category(),
                                "CreateService failed");

    SERVICE_PRESHUTDOWN_INFO preshutdown;
    preshutdown.dwPreshutdownTimeout = cPreshutdownInterval;
    if (!ChangeServiceConfig2(svc, SERVICE_CONFIG_PRESHUTDOWN_INFO, &preshutdown))
        throw std::system_error(GetLastError(), std::system_category(),
                                "ChangeServiceConfig2 failed");

    InstallEventLogSource(path.c_str());

    std::ostringstream ace;
    ace << "Granting read/write access to 'NT AUTHORITY\\NetworkService' on: ";
    for (const std::string &folder : access_paths(argc, argv)) {
        DWORD rc = add_ace(folder.c_str(), userName);
        if (rc != ERROR_SUCCESS) {
            fprintf(stderr, "ServiceInstall: ACL on \"%s\" failed rc=%lu\n",
                    folder.c_str(), rc);
        }
        ace << '"' << folder << "\" ";
    }
    service_print(ace.str());
    service_print("Redis successfully installed as a service.");

    char ok[512];
    snprintf(ok, sizeof(ok), "redis-server started as service '%s'",
             g_serviceName);
    /* Install does not start the service; success ID is for the install. */
    WriteEventLogSuccess("Redis service installed");
    (void)ok;
}

static void service_start(int argc, char **argv) {
    apply_service_name(argc, argv);
    ScHandle scm;
    scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (scm.invalid())
        throw std::system_error(GetLastError(), std::system_category(),
                                "OpenSCManager failed");
    ScHandle svc;
    svc = OpenServiceA(scm, g_serviceName, SERVICE_ALL_ACCESS);
    if (svc.invalid())
        throw std::system_error(GetLastError(), std::system_category(),
                                "OpenService failed");
    if (!StartServiceA(svc, 0, NULL))
        throw std::system_error(GetLastError(), std::system_category(),
                                "StartService failed");

    Sleep(2000);
    SERVICE_STATUS status;
    DWORD start = GetTickCount();
    while (QueryServiceStatus(svc, &status)) {
        if (status.dwCurrentState == SERVICE_RUNNING) {
            service_print("Redis service successfully started.");
            return;
        }
        if (status.dwCurrentState == SERVICE_STOPPED) {
            service_print("Redis service failed to start.");
            return;
        }
        if (GetTickCount() - start >= cThirtySeconds) {
            service_print("Redis service start timed out.");
            return;
        }
        Sleep(200);
    }
}

static void service_stop(int argc, char **argv) {
    apply_service_name(argc, argv);
    ScHandle scm;
    scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (scm.invalid())
        throw std::system_error(GetLastError(), std::system_category(),
                                "OpenSCManager failed");
    ScHandle svc;
    svc = OpenServiceA(scm, g_serviceName, SERVICE_ALL_ACCESS);
    if (svc.invalid())
        throw std::system_error(GetLastError(), std::system_category(),
                                "OpenService failed");
    SERVICE_STATUS status;
    if (!ControlService(svc, SERVICE_CONTROL_STOP, &status))
        throw std::system_error(GetLastError(), std::system_category(),
                                "ControlService failed");

    DWORD start = GetTickCount();
    while (QueryServiceStatus(svc, &status)) {
        if (status.dwCurrentState == SERVICE_STOPPED) {
            service_print("Redis service successfully stopped.");
            return;
        }
        if (GetTickCount() - start >= cThirtySeconds) {
            service_print("Redis service stop timed out.");
            return;
        }
        Sleep(200);
    }
}

static void service_uninstall(int argc, char **argv) {
    apply_service_name(argc, argv);
    ScHandle scm;
    scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (scm.invalid())
        throw std::system_error(GetLastError(), std::system_category(),
                                "OpenSCManager failed");
    ScHandle svc;
    svc = OpenServiceA(scm, g_serviceName, SERVICE_ALL_ACCESS);
    if (svc.valid()) {
        if (!DeleteService(svc))
            throw std::system_error(GetLastError(), std::system_category(),
                                    "DeleteService failed");
    }
    UninstallEventLogSource();
    service_print("Redis service successfully uninstalled.");
}

static DWORD WINAPI ServiceWorkerThread(LPVOID) {
    try {
        int argc = (int)serviceRunArguments.size();
        char **argv = new char *[argc];
        for (int i = 0; i < argc; i++) {
            const std::string &arg = serviceRunArguments[i];
            argv[i] = new char[arg.size() + 1];
            memcpy(argv[i], arg.c_str(), arg.size() + 1);
        }

        std::string currentDir = exe_dir();
        if (!SetCurrentDirectoryA(currentDir.c_str()))
            throw std::system_error(GetLastError(), std::system_category(),
                                    "SetCurrentDirectory failed");

        RedisWindowsParentMain(argc, argv);

        for (int i = 0; i < argc; i++)
            delete[] argv[i];
        delete[] argv;
        SetEvent(g_ServiceStoppedEvent);
        return ERROR_SUCCESS;
    } catch (const std::exception &ex) {
        fprintf(stderr, "ServiceWorkerThread: %s\n", ex.what());
        WriteEventLogError(ex.what());
    } catch (...) {
        fprintf(stderr, "ServiceWorkerThread: unknown exception\n");
    }
    SetEvent(g_ServiceStoppedEvent);
    return ERROR_PROCESS_ABORTED;
}

static void set_status(DWORD state, DWORD accepted, DWORD checkpoint) {
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = state;
    g_ServiceStatus.dwControlsAccepted = accepted;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwCheckPoint = checkpoint;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}

static DWORD WINAPI ServiceCtrlHandler(DWORD dwControl, DWORD, LPVOID, LPVOID) {
    switch (dwControl) {
    case SERVICE_CONTROL_PRESHUTDOWN:
    case SERVICE_CONTROL_STOP:
        if (g_ServiceStopEvent != INVALID_HANDLE_VALUE)
            SetEvent(g_ServiceStopEvent);
        set_status(SERVICE_STOP_PENDING, 0, 4);
        if (g_ServiceStoppedEvent != INVALID_HANDLE_VALUE) {
            DWORD start = GetTickCount();
            while (GetTickCount() - start < (DWORD)cPreshutdownInterval) {
                if (WaitForSingleObject(g_ServiceStoppedEvent, 1000) ==
                    WAIT_OBJECT_0)
                    break;
                set_status(SERVICE_STOP_PENDING, 0, 4);
            }
        }
        set_status(SERVICE_STOPPED, 0, 4);
        break;
    default:
        break;
    }
    return NO_ERROR;
}

static VOID WINAPI ServiceMain(DWORD, LPTSTR *) {
    g_StatusHandle = RegisterServiceCtrlHandlerExA(g_serviceName,
                                                   ServiceCtrlHandler, NULL);
    if (!g_StatusHandle)
        return;

    set_status(SERVICE_START_PENDING, 0, 0);

    g_ServiceStoppedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_ServiceStopEvent) {
        g_ServiceStatus.dwWin32ExitCode = GetLastError();
        set_status(SERVICE_STOPPED, 0, 1);
        return;
    }
    Win32ServiceSetStopEvent(g_ServiceStopEvent);

    set_status(SERVICE_RUNNING,
               SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN, 0);

    HANDLE hThread = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);
    if (hThread) {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
    }

    Win32ServiceSetStopEvent(NULL);
    if (g_ServiceStopEvent != INVALID_HANDLE_VALUE) {
        CloseHandle(g_ServiceStopEvent);
        g_ServiceStopEvent = INVALID_HANDLE_VALUE;
    }
    if (g_ServiceStoppedEvent != INVALID_HANDLE_VALUE) {
        CloseHandle(g_ServiceStoppedEvent);
        g_ServiceStoppedEvent = INVALID_HANDLE_VALUE;
    }
    set_status(SERVICE_STOPPED, 0, 3);
}

static void service_run(void) {
    SERVICE_TABLE_ENTRYA table[] = {
        {g_serviceName, (LPSERVICE_MAIN_FUNCTIONA)ServiceMain},
        {NULL, NULL}};
    if (!StartServiceCtrlDispatcherA(table))
        throw std::system_error(GetLastError(), std::system_category(),
                                "StartServiceCtrlDispatcherA failed");
}

static void build_service_run_arguments(int argc, char **argv) {
    apply_service_name(argc, argv);
    for (int n = 0; n < argc; n++) {
        if (n == 0) {
            serviceRunArguments.push_back(quote_arg(exe_path().c_str()));
        } else if (n == 1) {
            continue; /* --service-run */
        } else if (_stricmp(argv[n], "--service-name") == 0) {
            n++;
        } else {
            serviceRunArguments.push_back(argv[n]);
        }
    }
}

static const char *service_verb(const char *arg) {
    if (!arg || arg[0] != '-' || arg[1] != '-')
        return NULL;
    const char *s = arg + 2;
    if (_stricmp(s, "service-install") == 0)
        return "install";
    if (_stricmp(s, "service-uninstall") == 0)
        return "uninstall";
    if (_stricmp(s, "service-start") == 0)
        return "start";
    if (_stricmp(s, "service-stop") == 0)
        return "stop";
    if (_stricmp(s, "service-run") == 0)
        return "run";
    return NULL;
}

extern "C" int HandleServiceCommands(int argc, char **argv) {
    try {
        if (argc < 2)
            return 0;
        const char *verb = service_verb(argv[1]);
        if (!verb)
            return 0;

        if (strcmp(verb, "run") == 0) {
            g_isRunningAsService = 1;
            Win32ServiceSetRunning(1);
            apply_service_name(argc, argv);
            Win32ServiceSetName(g_serviceName);
            build_service_run_arguments(argc, argv);
            service_run();
            return 1;
        }

        if (!is_process_elevated())
            return relaunch_elevated(argc, argv);

        if (strcmp(verb, "install") == 0)
            service_install(argc, argv);
        else if (strcmp(verb, "uninstall") == 0)
            service_uninstall(argc, argv);
        else if (strcmp(verb, "start") == 0)
            service_start(argc, argv);
        else if (strcmp(verb, "stop") == 0)
            service_stop(argc, argv);
        return 1;
    } catch (const std::system_error &syserr) {
        std::ostringstream ss;
        ss << "HandleServiceCommands: system error " << syserr.code().value()
           << ": " << syserr.what();
        service_print(ss.str());
        WriteEventLogError(ss.str().c_str());
        exit(1);
    } catch (const std::exception &ex) {
        std::ostringstream ss;
        ss << "HandleServiceCommands: " << ex.what();
        service_print(ss.str());
        WriteEventLogError(ss.str().c_str());
        exit(1);
    } catch (...) {
        service_print("HandleServiceCommands: other exception caught.");
        exit(1);
    }
}


