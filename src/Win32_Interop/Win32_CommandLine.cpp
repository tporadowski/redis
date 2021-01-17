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

#include "win32fixes.h"
#include <mswsock.h>

#include "Win32_variadicFunctor.h"
#include "Win32_CommandLine.h"

// Win32_FDAPI.h includes modified winsock definitions that are useful in BindParam below. It
// also redefines the CRT close(FD) call as a macro. This conflicts with the fstream close
// definition. #undef solves the warning messages.
#undef close

#include <Shlwapi.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <exception>
#include <functional>
using namespace std;

#pragma comment (lib, "Shlwapi.lib")

ArgumentMap g_argMap;
vector<string> g_pathsAccessed;

string stripQuotes(string s) {
    if (s.length() >= 2) {
        if (s.at(0) == '\'' &&  s.at(s.length() - 1) == '\'') {
            if (s.length() > 2) {
                return s.substr(1, s.length() - 2);
            }
            else {
                return string("");
            }
        }
        if (s.at(0) == '\"' &&  s.at(s.length() - 1) == '\"') {
            if (s.length() > 2) {
                return s.substr(1, s.length() - 2);
            }
            else {
                return string("");
            }
        }
    }
    return s;
}

typedef class ParamExtractor {
public:
    ParamExtractor() {}
    virtual ~ParamExtractor() {}
    virtual vector<string> Extract(int argStartIndex, int argc, char** argv) = 0;
    virtual vector<string> Extract(vector<string> tokens, int StartIndex = 0) = 0;
} ParamExtractor;

typedef map<string, ParamExtractor*> RedisParameterMapper;

typedef class FixedParam : public ParamExtractor {
private:
    int parameterCount;

public:
    FixedParam(int count) {
        parameterCount = count;
    }

    vector<string> Extract(int argStartIndex, int argc, char** argv) {
        if (argStartIndex + parameterCount >= argc) {
            stringstream err;
            err << "Not enough parameters available for " << argv[argStartIndex];
            throw invalid_argument(err.str());
        }
        vector<string> params;
        for (int argIndex = argStartIndex + 1; argIndex < argStartIndex + 1 + parameterCount; argIndex++) {
            string param = string(argv[argIndex]);
            transform(param.begin(), param.end(), param.begin(), ::tolower);
            param = stripQuotes(param);
            params.push_back(param);
        }
        return params;
    }

    vector<string> Extract(vector<string> tokens, int startIndex = 0) {
        if ((int) (tokens.size() - 1) < parameterCount + startIndex) {
            stringstream err;
            err << "Not enough parameters available for " << tokens.at(0);
            throw invalid_argument(err.str());
        }
        vector<string> params;
        int skipCount = 1 + startIndex;
        for (string token : tokens) {
            if (skipCount > 0) {
                skipCount--;
                continue;
            }
            string param = string(token);
            transform(param.begin(), param.end(), param.begin(), ::tolower);
            param = stripQuotes(param);
            params.push_back(param);
        }
        return params;
    };
} FixedParam;

static FixedParam fp0 = FixedParam(0);
static FixedParam fp1 = FixedParam(1);
static FixedParam fp2 = FixedParam(2);
static FixedParam fp3 = FixedParam(3);
static FixedParam fp4 = FixedParam(4);

typedef class SaveParams : public ParamExtractor {
public:
    SaveParams() {}

    bool isStringAnInt(string test) {
        int x;
        char c;
        istringstream s(test);

        if (!(s >> x) ||            // not convertable to an int
            (s >> c)) {             // some character past the int
            return false;
        }
        else {
            return true;
        }
    }

    vector<string> Extract(int argStartIndex, int argc, char** argv) {
        vector<string> params;
        int argIndex = argStartIndex + 1;

        // save [seconds] [changes]
        // or
        // save ""      -- turns off RDB persistence
        if (strcmp(argv[argIndex], "\"\"") == 0 || strcmp(argv[argIndex], "''") == 0 || strcmp(argv[argIndex], "") == 0) {
            params.push_back(argv[argIndex]);
        }
        else if (
            isStringAnInt(argv[argIndex]) &&
            isStringAnInt(argv[argIndex + 1])) {
            params.push_back(argv[argIndex]);
            params.push_back(argv[argIndex + 1]);
        }
        else {
            stringstream err;
            err << "Not enough parameters available for " << argv[argStartIndex];
            throw invalid_argument(err.str());
        }
        return params;
    }

    virtual vector<string> Extract(vector<string> tokens, int startIndex = 0) {
        vector<string> params;
        unsigned int parameterIndex = 1 + startIndex;
        if ((tokens.size() > parameterIndex) &&
            (tokens.at(parameterIndex) == string("\"\"") ||
                tokens.at(parameterIndex) == string("''"))) {
            params.push_back(tokens.at(parameterIndex));
        }
        else if ((tokens.size() > parameterIndex + 1) &&
            isStringAnInt(tokens.at(parameterIndex)) &&
            isStringAnInt(tokens.at(parameterIndex + 1))) {
            params.push_back(tokens.at(parameterIndex));
            params.push_back(tokens.at(parameterIndex + 1));
        }
        else {
            stringstream err;
            err << "Not enough parameters available for " << tokens.at(startIndex);
            throw invalid_argument(err.str());
        }
        return params;
    };
} SaveParams;

static SaveParams savep = SaveParams();

typedef class BindParams : public ParamExtractor {
public:
    BindParams() {}

    dllfunctor_stdcall<int, LPCSTR, INT, LPWSAPROTOCOL_INFO, LPSOCKADDR, LPINT> f_WSAStringToAddressA =
        dllfunctor_stdcall<int, LPCSTR, INT, LPWSAPROTOCOL_INFO, LPSOCKADDR, LPINT>("ws2_32.dll", "WSAStringToAddressA");

    bool IsIPAddress(string address) {
        SOCKADDR_IN sockaddr4;
        sockaddr4.sin_family = AF_INET;
        SOCKADDR_IN6 sockaddr6;
        sockaddr6.sin6_family = AF_INET6;
        int addr4Length = sizeof(SOCKADDR_IN);
        int addr6Length = sizeof(SOCKADDR_IN6);
        DWORD err;
        if (ERROR_SUCCESS ==
            (err = f_WSAStringToAddressA(
                address.c_str(),
                AF_INET,
                NULL,
                (LPSOCKADDR) &sockaddr4,
                &addr4Length))) {
            return true;
        }
        else if (ERROR_SUCCESS ==
            (err = f_WSAStringToAddressA(
                address.c_str(),
                AF_INET6,
                NULL,
                (LPSOCKADDR) &sockaddr6,
                &addr6Length))) {
            return true;
        }
        else {
            return false;
        }
    }

    vector<string> Extract(int argStartIndex, int argc, char** argv) {
        vector<string> params;
        int argIndex = argStartIndex + 1;

        // bind [address1] [address2] ...
        while (argIndex < argc) {
            if (IsIPAddress(argv[argIndex])) {
                string param = string(argv[argIndex]);
                transform(param.begin(), param.end(), param.begin(), ::tolower);
                param = stripQuotes(param);
                params.push_back(param);
                argIndex++;
            }
            else {
                break;
            }
        }
        return params;
    }

    virtual vector<string> Extract(vector<string> tokens, int startIndex = 0) {
        vector<string> params;
        int skipCount = 1 + startIndex;
        for (string token : tokens) {
            if (skipCount > 0) {
                skipCount--;
                continue;
            }
            if (IsIPAddress(token)) {
                string param = string(token);
                transform(param.begin(), param.end(), param.begin(), ::tolower);
                param = stripQuotes(param);
                params.push_back(param);
            }
            else {
                break;
            }
        }
        return params;
    };
} BindParams;

static BindParams bp = BindParams();

typedef class SentinelParams : public  ParamExtractor {
private:
    RedisParameterMapper subCommands;

public:
    SentinelParams() {
        subCommands = RedisParameterMapper
        {
            //subcommands are parsed in src/sentinel.c/sentinelHandleConfiguration()
            { "monitor",                    &fp4 },    // sentinel monitor [master name] [ip] [port] [quorum]
            { "down-after-milliseconds",    &fp2 },    // sentinel down-after-milliseconds [master name] [milliseconds]
            { "failover-timeout",           &fp2 },    // sentinel failover-timeout [master name] [number]
            { "parallel-syncs",             &fp2 },    // sentinel parallel-syncs [master name] [number]
            { "notification-script",        &fp2 },    // sentinel notification-script [master name] [scriptPath]
            { "client-reconfig-script",     &fp2 },    // sentinel client-reconfig-script [master name] [scriptPath]
            { "auth-pass",                  &fp2 },    // sentinel auth-pass [master name] [password]
            { "current-epoch",              &fp1 },    // sentinel current-epoch <epoch>
            { "myid",                       &fp1 },    // sentinel myid <id>
            { "config-epoch",               &fp2 },    // sentinel config-epoch [name] [epoch]
            { "leader-epoch",               &fp2 },    // sentinel leader-epoch [name] [epoch]
            { "known-slave",                &fp3 },    // sentinel known-slave <name> <ip> <port>
            { "known-replica",              &fp3 },    // sentinel known-slave <name> <ip> <port>
            { "known-sentinel",             &fp4 },    // sentinel known-sentinel <name> <ip> <port> [runid]
            { "rename-command",             &fp3 },    // sentinel rename-command <name> <command> <renamed-command>
            { "announce-ip",                &fp1 },    // sentinel announce-ip <ip>
            { "announce-port",              &fp1 },    // sentinel announce-port <port>
            { "deny-scripts-reconfig",      &fp1 }     // sentinel deny-scripts-reconfig [yes/no]
        };
    }

    vector<string> Extract(int argStartIndex, int argc, char** argv) {
        stringstream err;
        if (argStartIndex + 1 >= argc) {
            err << "Not enough parameters available for " << argv[argStartIndex];
            throw invalid_argument(err.str());
        }
        if (subCommands.find(argv[argStartIndex + 1]) == subCommands.end()) {
            err << "Could not find sentinel subcommand " << argv[argStartIndex + 1];
            throw invalid_argument(err.str());
        }

        vector<string> params;
        params.push_back(argv[argStartIndex + 1]);
        vector<string> subParams = subCommands[argv[argStartIndex + 1]]->Extract(argStartIndex + 1, argc, argv);
        for (string p : subParams) {
            transform(p.begin(), p.end(), p.begin(), ::tolower);
            p = stripQuotes(p);
            params.push_back(p);
        }
        return params;
    }

    vector<string> Extract(vector<string> tokens, int startIndex = 0) {
        stringstream err;
        if (tokens.size() < 2) {
            err << "Not enough parameters available for " << tokens.at(0);
            throw invalid_argument(err.str());
        }
        string subcommand = tokens.at(startIndex + 1);
        if (subCommands.find(subcommand) == subCommands.end()) {
            err << "Could not find sentinel subcommand " << subcommand;
            throw invalid_argument(err.str());
        }

        vector<string> params;
        params.push_back(subcommand);

        vector<string> subParams = subCommands[subcommand]->Extract(tokens, startIndex + 1);

        for (string p : subParams) {
            transform(p.begin(), p.end(), p.begin(), ::tolower);
            p = stripQuotes(p);
            params.push_back(p);
        }
        return params;
    };
} SentinelParams;

static SentinelParams sp = SentinelParams();

//TODO: add validation like in BindParams?
typedef class UserParams : public ParamExtractor {
public:
    UserParams() {}

    vector<string> Extract(int argStartIndex, int argc, char** argv) {
        vector<string> params;
        int argIndex = argStartIndex + 1;

        // user <username> [acl rule, ...]
        while (argIndex < argc) {
            string param = string(argv[argIndex]);
            transform(param.begin(), param.end(), param.begin(), ::tolower);
            param = stripQuotes(param);
            params.push_back(param);
            argIndex++;
        }
        return params;
    }

    virtual vector<string> Extract(vector<string> tokens, int startIndex = 0) {
        vector<string> params;
        int skipCount = 1 + startIndex;
        for (string token : tokens) {
            if (skipCount > 0) {
                skipCount--;
                continue;
            }
            string param = string(token);
            transform(param.begin(), param.end(), param.begin(), ::tolower);
            param = stripQuotes(param);
            params.push_back(param);
        }
        return params;
    };
} UserParams;

static UserParams userp = UserParams();

// Map of argument name to argument processing engine.
static RedisParameterMapper g_redisArgMap =
{
    // QFork flags
    { cQFork,                           &fp2 },    // qfork [QForkControlMemoryMap handle] [parent process id]
    { cPersistenceAvailable,            &fp1 },    // persistence-available [yes/no]

    // service commands
    { cServiceName,                     &fp1 },    // service-name [name]
    { cServiceRun,                      &fp0 },    // service-run
    { cServiceInstall,                  &fp0 },    // service-install
    { cServiceUninstall,                &fp0 },    // service-uninstall
    { cServiceStart,                    &fp0 },    // service-start
    { cServiceStop,                     &fp0 },    // service-stop

    // Redis standard configs (as defined in src/config.c/configs[])
    // createBoolConfig("rdbchecksum", NULL, IMMUTABLE_CONFIG, server.rdb_checksum, 1, NULL, NULL),
    { "rdbchecksum",                    &fp1 },    // rdbchecksum [yes/no]
    // createBoolConfig("daemonize", NULL, IMMUTABLE_CONFIG, server.daemonize, 0, NULL, NULL),
    { "daemonize",                      &fp1 },    // daemonize [yes/no]
    // createBoolConfig("io-threads-do-reads", NULL, IMMUTABLE_CONFIG, server.io_threads_do_reads, 0,NULL, NULL), /* Read + parse from threads? */
    { "io-threads-do-reads",            &fp1 },    // io-threads-do-reads [yes/no]
    // createBoolConfig("lua-replicate-commands", NULL, MODIFIABLE_CONFIG, server.lua_always_replicate_commands, 1, NULL, NULL),
    { "lua-replicate-commands",         &fp1 },    // lua-replicate-commands [yes/no]
    // createBoolConfig("always-show-logo", NULL, IMMUTABLE_CONFIG, server.always_show_logo, 0, NULL, NULL),
    { "always-show-logo",               &fp1 },    // always-show-logo [yes/no]
    // createBoolConfig("protected-mode", NULL, MODIFIABLE_CONFIG, server.protected_mode, 1, NULL, NULL),
    { "protected-mode",                 &fp1 },    // protected-mode [yes/no]
    // createBoolConfig("rdbcompression", NULL, MODIFIABLE_CONFIG, server.rdb_compression, 1, NULL, NULL),
    { "rdbcompression",                 &fp1 },    // rdbcompression [yes/no]
    // createBoolConfig("rdb-del-sync-files", NULL, MODIFIABLE_CONFIG, server.rdb_del_sync_files, 0, NULL, NULL),
    { "rdb-del-sync-files",             &fp1 },    // rdb-del-sync-files [yes/no]
    // createBoolConfig("activerehashing", NULL, MODIFIABLE_CONFIG, server.activerehashing, 1, NULL, NULL),
    { "activerehashing",                &fp1 },    // activerehashing [yes/no]
    // createBoolConfig("stop-writes-on-bgsave-error", NULL, MODIFIABLE_CONFIG, server.stop_writes_on_bgsave_err, 1, NULL, NULL),
    { "stop-writes-on-bgsave-error",    &fp1 },    // stop-writes-on-bgsave-error [yes/no]
    // createBoolConfig("dynamic-hz", NULL, MODIFIABLE_CONFIG, server.dynamic_hz, 1, NULL, NULL), /* Adapt hz to # of clients.*/
    { "dynamic-hz",                     &fp1 },    // dynamic-hz [yes/no]
    // createBoolConfig("lazyfree-lazy-eviction", NULL, MODIFIABLE_CONFIG, server.lazyfree_lazy_eviction, 0, NULL, NULL),
    { "lazyfree-lazy-eviction",         &fp1 },    // lazyfree-lazy-eviction [yes/no]
    // createBoolConfig("lazyfree-lazy-expire", NULL, MODIFIABLE_CONFIG, server.lazyfree_lazy_expire, 0, NULL, NULL),
    { "lazyfree-lazy-expire",           &fp1 },    // lazyfree-lazy-expire [yes/no]
    // createBoolConfig("lazyfree-lazy-server-del", NULL, MODIFIABLE_CONFIG, server.lazyfree_lazy_server_del, 0, NULL, NULL),
    { "lazyfree-lazy-server-del",       &fp1 },    // lazyfree-lazy-server-del [yes/no]
    // createBoolConfig("lazyfree-lazy-user-del", NULL, MODIFIABLE_CONFIG, server.lazyfree_lazy_user_del , 0, NULL, NULL),
    { "lazyfree-lazy-user-del",         &fp1 },    // lazyfree-lazy-user-del [yes/no]
    // createBoolConfig("repl-disable-tcp-nodelay", NULL, MODIFIABLE_CONFIG, server.repl_disable_tcp_nodelay, 0, NULL, NULL),
    { "repl-disable-tcp-nodelay",       &fp1 },    // repl-disable-tcp-nodelay [yes/no]
    // createBoolConfig("repl-diskless-sync", NULL, MODIFIABLE_CONFIG, server.repl_diskless_sync, 0, NULL, NULL),
    { "repl-diskless-sync",             &fp1 },    // repl-diskless-sync [yes/no]
    // createBoolConfig("gopher-enabled", NULL, MODIFIABLE_CONFIG, server.gopher_enabled, 0, NULL, NULL),
    { "gopher-enabled",                 &fp1 },    // gopher-enabled [yes/no]
    // createBoolConfig("aof-rewrite-incremental-fsync", NULL, MODIFIABLE_CONFIG, server.aof_rewrite_incremental_fsync, 1, NULL, NULL),
    { "aof-rewrite-incremental-fsync",  &fp1 },    // aof-rewrite-incremental-fsync [yes/no]
    // createBoolConfig("no-appendfsync-on-rewrite", NULL, MODIFIABLE_CONFIG, server.aof_no_fsync_on_rewrite, 0, NULL, NULL),
    { "no-appendfsync-on-rewrite",      &fp1 },    // no-appendfsync-on-rewrite [value]
    // createBoolConfig("cluster-require-full-coverage", NULL, MODIFIABLE_CONFIG, server.cluster_require_full_coverage, 1, NULL, NULL),
    { "cluster-require-full-coverage",  &fp1 },    // cluster-require-full-coverage [yes/no]
    // createBoolConfig("rdb-save-incremental-fsync", NULL, MODIFIABLE_CONFIG, server.rdb_save_incremental_fsync, 1, NULL, NULL),
    { "rdb-save-incremental-fsync",     &fp1 },    // rdb-save-incremental-fsync [yes/no]
    // createBoolConfig("aof-load-truncated", NULL, MODIFIABLE_CONFIG, server.aof_load_truncated, 1, NULL, NULL),
    { "aof-load-truncated",             &fp1 },    // aof-load-truncated [yes/no]
    // createBoolConfig("aof-use-rdb-preamble", NULL, MODIFIABLE_CONFIG, server.aof_use_rdb_preamble, 1, NULL, NULL),
    { "aof-use-rdb-preamble",           &fp1 },    // aof-use-rdb-preamble [yes/no]
    // createBoolConfig("cluster-replica-no-failover", "cluster-slave-no-failover", MODIFIABLE_CONFIG, server.cluster_slave_no_failover, 0, NULL, NULL), /* Failover by default. */
    { "cluster-replica-no-failover",    &fp1 },    // cluster-replica-no-failover [yes/no]
    { "cluster-slave-no-failover",      &fp1 },    // cluster-slave-no-failover [yes/no]
    // createBoolConfig("replica-lazy-flush", "slave-lazy-flush", MODIFIABLE_CONFIG, server.repl_slave_lazy_flush, 0, NULL, NULL),
    { "replica-lazy-flush",             &fp1 },    // replica-lazy-flush [yes/no]
    { "slave-lazy-flush",               &fp1 },    // slave-lazy-flush [yes/no]
    // createBoolConfig("replica-serve-stale-data", "slave-serve-stale-data", MODIFIABLE_CONFIG, server.repl_serve_stale_data, 1, NULL, NULL),
    { "replica-serve-stale-data",       &fp1 },    // replica-serve-stale-data [yes/no]
    { "slave-serve-stale-data",         &fp1 },    // slave-serve-stale-data [yes/no]
    // createBoolConfig("replica-read-only", "slave-read-only", MODIFIABLE_CONFIG, server.repl_slave_ro, 1, NULL, NULL),
    { "replica-read-only",              &fp1 },    // replica-read-only [yes/no]
    { "slave-read-only",                &fp1 },    // slave-read-only [yes/no]
    // createBoolConfig("replica-ignore-maxmemory", "slave-ignore-maxmemory", MODIFIABLE_CONFIG, server.repl_slave_ignore_maxmemory, 1, NULL, NULL),
    { "replica-ignore-maxmemory",       &fp1 },    // replica-ignore-maxmemory [yes/no]
    { "slave-ignore-maxmemory",         &fp1 },    // slave-ignore-maxmemory [yes/no]
    // createBoolConfig("jemalloc-bg-thread", NULL, MODIFIABLE_CONFIG, server.jemalloc_bg_thread, 1, NULL, updateJemallocBgThread),
    { "jemalloc-bg-thread",             &fp1 },    // jemalloc-bg-thread [yes/no]
    // createBoolConfig("activedefrag", NULL, MODIFIABLE_CONFIG, server.active_defrag_enabled, 0, isValidActiveDefrag, NULL),
    { "activedefrag",                   &fp1 },    // activedefrag [yes/no]
    // createBoolConfig("syslog-enabled", NULL, IMMUTABLE_CONFIG, server.syslog_enabled, 0, NULL, NULL),
    { "syslog-enabled",                 &fp1 },    // syslog-enabled [yes/no]
    // createBoolConfig("cluster-enabled", NULL, IMMUTABLE_CONFIG, server.cluster_enabled, 0, NULL, NULL),
    { "cluster-enabled",                &fp1 },    // cluster-enabled [yes/no]
    // createBoolConfig("appendonly", NULL, MODIFIABLE_CONFIG, server.aof_enabled, 0, NULL, updateAppendonly),
    { "appendonly",                     &fp1 },    // appendonly [yes/no]
    // createBoolConfig("cluster-allow-reads-when-down", NULL, MODIFIABLE_CONFIG, server.cluster_allow_reads_when_down, 0, NULL, NULL),
    { "cluster-allow-reads-when-down",  &fp1 },    // cluster-allow-reads-when-down [yes/no]
    // createBoolConfig("oom-score-adj", NULL, MODIFIABLE_CONFIG, server.oom_score_adj, 0, NULL, updateOOMScoreAdj),
    { "oom-score-adj",                  &fp1 },    // oom-score-adj [yes/no]
    // createStringConfig("aclfile", NULL, IMMUTABLE_CONFIG, ALLOW_EMPTY_STRING, server.acl_filename, "", NULL, NULL),
    { "aclfile",                        &fp1 },    // aclfile [path]
    // createStringConfig("unixsocket", NULL, IMMUTABLE_CONFIG, EMPTY_STRING_IS_NULL, server.unixsocket, NULL, NULL, NULL),
    { "unixsocket",                     &fp1 },    // unixsocket [path]
    // createStringConfig("pidfile", NULL, IMMUTABLE_CONFIG, EMPTY_STRING_IS_NULL, server.pidfile, NULL, NULL, NULL),
    { "pidfile",                        &fp1 },    // pidfile [file]
    // createStringConfig("replica-announce-ip", "slave-announce-ip", MODIFIABLE_CONFIG, EMPTY_STRING_IS_NULL, server.slave_announce_ip, NULL, NULL, NULL),
    { "replica-announce-ip",            &fp1 },    // replica-announce-ip [string]
    { "slave-announce-ip",              &fp1 },    // slave-announce-ip [string]
    // createStringConfig("masteruser", NULL, MODIFIABLE_CONFIG, EMPTY_STRING_IS_NULL, server.masteruser, NULL, NULL, NULL),
    { "masteruser",                     &fp1 },    // masteruser [string]
    // createStringConfig("masterauth", NULL, MODIFIABLE_CONFIG, EMPTY_STRING_IS_NULL, server.masterauth, NULL, NULL, NULL),
    { "masterauth",                     &fp1 },    // masterauth [master-password]
    // createStringConfig("cluster-announce-ip", NULL, MODIFIABLE_CONFIG, EMPTY_STRING_IS_NULL, server.cluster_announce_ip, NULL, NULL, NULL),
    { "cluster-announce-ip",            &fp1 },    // cluster-announce-ip [string]
    // createStringConfig("syslog-ident", NULL, IMMUTABLE_CONFIG, ALLOW_EMPTY_STRING, server.syslog_ident, "redis", NULL, NULL),
    { "syslog-ident",                   &fp1 },    // syslog-ident [string]
    // createStringConfig("dbfilename", NULL, MODIFIABLE_CONFIG, ALLOW_EMPTY_STRING, server.rdb_filename, "dump.rdb", isValidDBfilename, NULL),
    { "dbfilename",                     &fp1 },    // dbfilename [filename]
    // createStringConfig("appendfilename", NULL, IMMUTABLE_CONFIG, ALLOW_EMPTY_STRING, server.aof_filename, "appendonly.aof", isValidAOFfilename, NULL),
    { "appendfilename",                 &fp1 },    // appendfilename [filename]
    // createStringConfig("server_cpulist", NULL, IMMUTABLE_CONFIG, EMPTY_STRING_IS_NULL, server.server_cpulist, NULL, NULL, NULL),
    { "server_cpulist",                 &fp1 },    // server_cpulist [string]
    // createStringConfig("bio_cpulist", NULL, IMMUTABLE_CONFIG, EMPTY_STRING_IS_NULL, server.bio_cpulist, NULL, NULL, NULL),
    { "bio_cpulist",                    &fp1 },    // bio_cpulist [string]
    // createStringConfig("aof_rewrite_cpulist", NULL, IMMUTABLE_CONFIG, EMPTY_STRING_IS_NULL, server.aof_rewrite_cpulist, NULL, NULL, NULL),
    { "aof_rewrite_cpulist",            &fp1 },    // aof_rewrite_cpulist [string]
    // createStringConfig("bgsave_cpulist", NULL, IMMUTABLE_CONFIG, EMPTY_STRING_IS_NULL, server.bgsave_cpulist, NULL, NULL, NULL),
    { "bgsave_cpulist",                 &fp1 },    // bgsave_cpulist [string]
    // createEnumConfig("supervised", NULL, IMMUTABLE_CONFIG, supervised_mode_enum, server.supervised_mode, SUPERVISED_NONE, NULL, NULL),
    { "supervised",                     &fp1 },    // supervised [upstart|systemd|auto|no]
    // createEnumConfig("syslog-facility", NULL, IMMUTABLE_CONFIG, syslog_facility_enum, server.syslog_facility, LOG_LOCAL0, NULL, NULL),
    { "syslog-facility",                &fp1 },    // syslog-facility [string]
    // createEnumConfig("repl-diskless-load", NULL, MODIFIABLE_CONFIG, repl_diskless_load_enum, server.repl_diskless_load, REPL_DISKLESS_LOAD_DISABLED, NULL, NULL),
    { "repl-diskless-load",             &fp1 },    // repl-diskless-load [disabled|on-empty-db|swapdb]
    // createEnumConfig("loglevel", NULL, MODIFIABLE_CONFIG, loglevel_enum, server.verbosity, LL_NOTICE, NULL, IF_WIN32(updateLogLevel, NULL)),
    { "loglevel",                       &fp1 },    // lovlevel [value]
    // createEnumConfig("maxmemory-policy", NULL, MODIFIABLE_CONFIG, maxmemory_policy_enum, server.maxmemory_policy, MAXMEMORY_NO_EVICTION, NULL, NULL),
    { "maxmemory-policy",               &fp1 },    // maxmemory-policy [volatile-lru|volatile-lfu|volatile-random|volatile-ttl|allkeys-lru|allkeys-lfu|allkeys-random|noeviction]
    // createEnumConfig("appendfsync", NULL, MODIFIABLE_CONFIG, aof_fsync_enum, server.aof_fsync, AOF_FSYNC_EVERYSEC, NULL, NULL),
    { "appendfsync",                    &fp1 },    // appendfsync [value]
    // createIntConfig("databases", NULL, IMMUTABLE_CONFIG, 1, INT_MAX, server.dbnum, 16, INTEGER_CONFIG, NULL, NULL),
    { "databases",                      &fp1 },    // databases [number]
    // createIntConfig("port", NULL, IMMUTABLE_CONFIG, 0, 65535, server.port, 6379, INTEGER_CONFIG, NULL, NULL), /* TCP port. */
    { "port",                           &fp1 },    // port [port number]
    // createIntConfig("io-threads", NULL, IMMUTABLE_CONFIG, 1, 128, server.io_threads_num, 1, INTEGER_CONFIG, NULL, NULL), /* Single threaded by default */
    { "io-threads",                     &fp1 },    // io-threads [number]
    // createIntConfig("auto-aof-rewrite-percentage", NULL, MODIFIABLE_CONFIG, 0, INT_MAX, server.aof_rewrite_perc, 100, INTEGER_CONFIG, NULL, NULL),
    { "auto-aof-rewrite-percentage",    &fp1 },    // auto-aof-rewrite-percentage [number]
    // createIntConfig("cluster-replica-validity-factor", "cluster-slave-validity-factor", MODIFIABLE_CONFIG, 0, INT_MAX, server.cluster_slave_validity_factor, 10, INTEGER_CONFIG, NULL, NULL), /* Slave max data age factor. */
    { "cluster-replica-validity-factor",&fp1 },    // cluster-replica-validity-factor [number]
    { "cluster-slave-validity-factor",  &fp1 },    // cluster-slave-validity-factor [number]
    // createIntConfig("list-max-ziplist-size", NULL, MODIFIABLE_CONFIG, INT_MIN, INT_MAX, server.list_max_ziplist_size, -2, INTEGER_CONFIG, NULL, NULL),
    { "list-max-ziplist-size",          &fp1 },    // list-max-ziplist-size [number]
    // createIntConfig("tcp-keepalive", NULL, MODIFIABLE_CONFIG, 0, INT_MAX, server.tcpkeepalive, 300, INTEGER_CONFIG, NULL, NULL),
    { "tcp-keepalive",                  &fp1 },    // tcp-keepalive [value]
    // createIntConfig("cluster-migration-barrier", NULL, MODIFIABLE_CONFIG, 0, INT_MAX, server.cluster_migration_barrier, 1, INTEGER_CONFIG, NULL, NULL),
    { "cluster-migration-barrier",      &fp1 },    // cluster-migration-barrier [number]
    // createIntConfig("active-defrag-cycle-min", NULL, MODIFIABLE_CONFIG, 1, 99, server.active_defrag_cycle_min, 1, INTEGER_CONFIG, NULL, NULL), /* Default: 1% CPU min (at lower threshold) */
    { "active-defrag-cycle-min",        &fp1 },    // active-defrag-cycle-min [number]
    // createIntConfig("active-defrag-cycle-max", NULL, MODIFIABLE_CONFIG, 1, 99, server.active_defrag_cycle_max, 25, INTEGER_CONFIG, NULL, NULL), /* Default: 25% CPU max (at upper threshold) */
    { "active-defrag-cycle-max",        &fp1 },    // active-defrag-cycle-max [number]
    // createIntConfig("active-defrag-threshold-lower", NULL, MODIFIABLE_CONFIG, 0, 1000, server.active_defrag_threshold_lower, 10, INTEGER_CONFIG, NULL, NULL), /* Default: don't defrag when fragmentation is below 10% */
    { "active-defrag-threshold-lower",  &fp1 },    // active-defrag-threshold-lower [number]
    // createIntConfig("active-defrag-threshold-upper", NULL, MODIFIABLE_CONFIG, 0, 1000, server.active_defrag_threshold_upper, 100, INTEGER_CONFIG, NULL, NULL), /* Default: maximum defrag force at 100% fragmentation */
    { "active-defrag-threshold-upper",  &fp1 },    // active-defrag-threshold-upper [number]
    // createIntConfig("lfu-log-factor", NULL, MODIFIABLE_CONFIG, 0, INT_MAX, server.lfu_log_factor, 10, INTEGER_CONFIG, NULL, NULL),
    { "lfu-log-factor",                 &fp1 },    // lfu-log-factor [number]
    // createIntConfig("lfu-decay-time", NULL, MODIFIABLE_CONFIG, 0, INT_MAX, server.lfu_decay_time, 1, INTEGER_CONFIG, NULL, NULL),
    { "lfu-decay-time",                 &fp1 },    // lfu-decay-time [number]
    // createIntConfig("replica-priority", "slave-priority", MODIFIABLE_CONFIG, 0, INT_MAX, server.slave_priority, 100, INTEGER_CONFIG, NULL, NULL),
    { "replica-priority",               &fp1 },    // replica-priority [number]
    { "slave-priority",                 &fp1 },    // slave-priority [number]
     // createIntConfig("repl-diskless-sync-delay", NULL, MODIFIABLE_CONFIG, 0, INT_MAX, server.repl_diskless_sync_delay, 5, INTEGER_CONFIG, NULL, NULL),
    { "repl-diskless-sync-delay",       &fp1 },    // repl-diskless-sync-delay [number]
    // createIntConfig("maxmemory-samples", NULL, MODIFIABLE_CONFIG, 1, INT_MAX, server.maxmemory_samples, 5, INTEGER_CONFIG, NULL, NULL),
    { "maxmemory-samples",              &fp1 },    // maxmemory-samples [number]
    // createIntConfig("timeout", NULL, MODIFIABLE_CONFIG, 0, INT_MAX, server.maxidletime, 0, INTEGER_CONFIG, NULL, NULL), /* Default client timeout: infinite */
    { "timeout",                        &fp1 },    // timeout [value]
    // createIntConfig("replica-announce-port", "slave-announce-port", MODIFIABLE_CONFIG, 0, 65535, server.slave_announce_port, 0, INTEGER_CONFIG, NULL, NULL),
    { "replica-announce-port",          &fp1 },    // replica-announce-port [number]
    { "slave-announce-port",            &fp1 },    // slave-announce-port [number]
    // createIntConfig("tcp-backlog", NULL, IMMUTABLE_CONFIG, 0, INT_MAX, server.tcp_backlog, 511, INTEGER_CONFIG, NULL, NULL), /* TCP listen backlog. */
    { "tcp-backlog",                    &fp1 },    // tcp-backlog [number]
    // createIntConfig("cluster-announce-bus-port", NULL, MODIFIABLE_CONFIG, 0, 65535, server.cluster_announce_bus_port, 0, INTEGER_CONFIG, NULL, NULL), /* Default: Use +10000 offset. */
    { "cluster-announce-bus-port",      &fp1 },    // cluster-announce-bus-port [number]
    // createIntConfig("cluster-announce-port", NULL, MODIFIABLE_CONFIG, 0, 65535, server.cluster_announce_port, 0, INTEGER_CONFIG, NULL, NULL), /* Use server.port */
    { "cluster-announce-port",          &fp1 },    // cluster-announce-port [number]
    // createIntConfig("repl-timeout", NULL, MODIFIABLE_CONFIG, 1, INT_MAX, server.repl_timeout, 60, INTEGER_CONFIG, NULL, NULL),
    { "repl-timeout",                   &fp1 },    // repl-timeout [number]
    // createIntConfig("repl-ping-replica-period", "repl-ping-slave-period", MODIFIABLE_CONFIG, 1, INT_MAX, server.repl_ping_slave_period, 10, INTEGER_CONFIG, NULL, NULL),
    { "repl-ping-replica-period",       &fp1 },    // repl-ping-replica-period [number]
    { "repl-ping-slave-period",         &fp1 },    // repl-ping-slave-period [number]
    // createIntConfig("list-compress-depth", NULL, MODIFIABLE_CONFIG, 0, INT_MAX, server.list_compress_depth, 0, INTEGER_CONFIG, NULL, NULL),
    { "list-compress-depth",            &fp1 },    // list-compress-depth [number]
    // createIntConfig("rdb-key-save-delay", NULL, MODIFIABLE_CONFIG, 0, INT_MAX, server.rdb_key_save_delay, 0, INTEGER_CONFIG, NULL, NULL),
    { "rdb-key-save-delay",             &fp1 },    // rdb-key-save-delay [number]
    // createIntConfig("key-load-delay", NULL, MODIFIABLE_CONFIG, 0, INT_MAX, server.key_load_delay, 0, INTEGER_CONFIG, NULL, NULL),
    { "key-load-delay",                 &fp1 },    // key-load-delay [number]
    // createIntConfig("active-expire-effort", NULL, MODIFIABLE_CONFIG, 1, 10, server.active_expire_effort, 1, INTEGER_CONFIG, NULL, NULL), /* From 1 to 10. */
    { "active-expire-effort",           &fp1 },    // active-expire-effort [number]
    // createIntConfig("hz", NULL, MODIFIABLE_CONFIG, 0, INT_MAX, server.config_hz, CONFIG_DEFAULT_HZ, INTEGER_CONFIG, NULL, updateHZ),
    { "hz",                             &fp1 },    // hz [number]
    // createIntConfig("min-replicas-to-write", "min-slaves-to-write", MODIFIABLE_CONFIG, 0, INT_MAX, server.repl_min_slaves_to_write, 0, INTEGER_CONFIG, NULL, updateGoodSlaves),
    { "min-replicas-to-write",          &fp1 },    // min-replicas-to-write [number]
    { "min-slaves-to-write",            &fp1 },    // min-slaves-to-write [number]
    // createIntConfig("min-replicas-max-lag", "min-slaves-max-lag", MODIFIABLE_CONFIG, 0, INT_MAX, server.repl_min_slaves_max_lag, 10, INTEGER_CONFIG, NULL, updateGoodSlaves),
    { "min-replicas-max-lag",           &fp1 },    // min-replicas-max-lag [number]
    { "min-slaves-max-lag",             &fp1 },    // min-slaves-max-lag [number]
    // createUIntConfig("maxclients", NULL, MODIFIABLE_CONFIG, 1, UINT_MAX, server.maxclients, 10000, INTEGER_CONFIG, NULL, updateMaxclients),
    { "maxclients",                     &fp1 },    // maxclients [number]
    // createULongConfig("active-defrag-max-scan-fields", NULL, MODIFIABLE_CONFIG, 1, LONG_MAX, server.active_defrag_max_scan_fields, 1000, INTEGER_CONFIG, NULL, NULL), /* Default: keys with more than 1000 fields will be processed separately */
    { "active-defrag-max-scan-fields",  &fp1 },    // active-defrag-max-scan-fields [number]
    // createULongConfig("slowlog-max-len", NULL, MODIFIABLE_CONFIG, 0, LONG_MAX, server.slowlog_max_len, 128, INTEGER_CONFIG, NULL, NULL),
    { "slowlog-max-len",                &fp1 },    // slowlog-max-len [number]
    // createULongConfig("acllog-max-len", NULL, MODIFIABLE_CONFIG, 0, LONG_MAX, server.acllog_max_len, 128, INTEGER_CONFIG, NULL, NULL),
    { "acllog-max-len",                 &fp1 },    // acllog-max-len [number]
    // createLongLongConfig("lua-time-limit", NULL, MODIFIABLE_CONFIG, 0, LONG_MAX, server.lua_time_limit, 5000, INTEGER_CONFIG, NULL, NULL),/* milliseconds */
    { "lua-time-limit",                 &fp1 },    // lua-time-limit [number]
    // createLongLongConfig("cluster-node-timeout", NULL, MODIFIABLE_CONFIG, 0, LLONG_MAX, server.cluster_node_timeout, 15000, INTEGER_CONFIG, NULL, NULL),
    { "cluster-node-timeout",           &fp1 },    // cluster-node-timeout [number]
    // createLongLongConfig("slowlog-log-slower-than", NULL, MODIFIABLE_CONFIG, -1, LLONG_MAX, server.slowlog_log_slower_than, 10000, INTEGER_CONFIG, NULL, NULL),
    { "slowlog-log-slower-than",        &fp1 },    // slowlog-log-slower-than [number]
    // createLongLongConfig("latency-monitor-threshold", NULL, MODIFIABLE_CONFIG, 0, LLONG_MAX, server.latency_monitor_threshold, 0, INTEGER_CONFIG, NULL, NULL),
    { "latency-monitor-threshold",      &fp1 },    // latency-monitor-threshold [number]
    // createLongLongConfig("proto-max-bulk-len", NULL, MODIFIABLE_CONFIG, 1024*1024, LLONG_MAX, server.proto_max_bulk_len, 512ll*1024*1024, MEMORY_CONFIG, NULL, NULL), /* Bulk request max size */
    { "proto-max-bulk-len",             &fp1 },    // proto-max-bulk-len [number]
    // createLongLongConfig("stream-node-max-entries", NULL, MODIFIABLE_CONFIG, 0, LLONG_MAX, server.stream_node_max_entries, 100, INTEGER_CONFIG, NULL, NULL),
    { "stream-node-max-entries",        &fp1 },    // stream-node-max-entries [number]
    // createLongLongConfig("repl-backlog-size", NULL, MODIFIABLE_CONFIG, 1, LLONG_MAX, server.repl_backlog_size, 1024*1024, MEMORY_CONFIG, NULL, updateReplBacklogSize), /* Default: 1mb */
    { "repl-backlog-size",              &fp1 },    // repl-backlog-size [number]
    // createULongLongConfig("maxmemory", NULL, MODIFIABLE_CONFIG, 0, ULLONG_MAX, server.maxmemory, 0, MEMORY_CONFIG, NULL, updateMaxmemory),
    { "maxmemory",                      &fp1 },    // maxmemory [bytes]
    // createSizeTConfig("hash-max-ziplist-entries", NULL, MODIFIABLE_CONFIG, 0, LONG_MAX, server.hash_max_ziplist_entries, 512, INTEGER_CONFIG, NULL, NULL),
    { "hash-max-ziplist-entries",       &fp1 },    // hash-max-ziplist-entries [number]
    // createSizeTConfig("set-max-intset-entries", NULL, MODIFIABLE_CONFIG, 0, LONG_MAX, server.set_max_intset_entries, 512, INTEGER_CONFIG, NULL, NULL),
    { "set-max-intset-entries",         &fp1 },    // set-max-intset-entries [number]
    // createSizeTConfig("zset-max-ziplist-entries", NULL, MODIFIABLE_CONFIG, 0, LONG_MAX, server.zset_max_ziplist_entries, 128, INTEGER_CONFIG, NULL, NULL),
    { "zset-max-ziplist-entries",       &fp1 },    // zset-max-ziplist-entries [number]
    // createSizeTConfig("active-defrag-ignore-bytes", NULL, MODIFIABLE_CONFIG, 1, LLONG_MAX, server.active_defrag_ignore_bytes, 100<<20, MEMORY_CONFIG, NULL, NULL), /* Default: don't defrag if frag overhead is below 100mb */
    { "active-defrag-ignore-bytes",     &fp1 },    // active-defrag-ignore-bytes [number]
    // createSizeTConfig("hash-max-ziplist-value", NULL, MODIFIABLE_CONFIG, 0, LONG_MAX, server.hash_max_ziplist_value, 64, MEMORY_CONFIG, NULL, NULL),
    { "hash-max-ziplist-value",         &fp1 },    // hash-max-ziplist-value [number]
    // createSizeTConfig("stream-node-max-bytes", NULL, MODIFIABLE_CONFIG, 0, LONG_MAX, server.stream_node_max_bytes, 4096, MEMORY_CONFIG, NULL, NULL),
    { "stream-node-max-bytes",          &fp1 },    // stream-node-max-bytes [number]
    // createSizeTConfig("zset-max-ziplist-value", NULL, MODIFIABLE_CONFIG, 0, LONG_MAX, server.zset_max_ziplist_value, 64, MEMORY_CONFIG, NULL, NULL),
    { "zset-max-ziplist-value",         &fp1 },    // zset-max-ziplist-value [number]
    // createSizeTConfig("hll-sparse-max-bytes", NULL, MODIFIABLE_CONFIG, 0, LONG_MAX, server.hll_sparse_max_bytes, 3000, MEMORY_CONFIG, NULL, NULL),
    { "hll-sparse-max-bytes",           &fp1 },    // hll-sparse-max-bytes [number]
    // createSizeTConfig("tracking-table-max-keys", NULL, MODIFIABLE_CONFIG, 0, LONG_MAX, server.tracking_table_max_keys, 1000000, INTEGER_CONFIG, NULL, NULL), /* Default: 1 million keys max. */
    { "tracking-table-max-keys",        &fp1 },    // tracking-table-max-keys [number]
    // createTimeTConfig("repl-backlog-ttl", NULL, MODIFIABLE_CONFIG, 0, LONG_MAX, server.repl_backlog_time_limit, 60*60, INTEGER_CONFIG, NULL, NULL), /* Default: 1 hour */
    { "repl-backlog-ttl",               &fp1 },    // repl-backlog-ttl [number]
    // createOffTConfig("auto-aof-rewrite-min -size", NULL, MODIFIABLE_CONFIG, 0, LLONG_MAX, server.aof_rewrite_min_size, 64*1024*1024, MEMORY_CONFIG, NULL, NULL),
    { "auto-aof-rewrite-min-size",      &fp1 },    // auto-aof-rewrite-min-size [number]

#ifdef USE_OPENSSL
    // createIntConfig("tls-port", NULL, IMMUTABLE_CONFIG, 0, 65535, server.tls_port, 0, INTEGER_CONFIG, NULL, updateTlsCfgInt), /* TCP port. */
    { "tls-port",                       &fp1 },    // tls-port [number]
    // createIntConfig("tls-session-cache-size", NULL, MODIFIABLE_CONFIG, 0, INT_MAX, server.tls_ctx_config.session_cache_size, 20*1024, INTEGER_CONFIG, NULL, updateTlsCfgInt),
    { "tls-session-cache-size",         &fp1 },    // tls-session-cache-size [number]
    // createIntConfig("tls-session-cache-timeout", NULL, MODIFIABLE_CONFIG, 0, INT_MAX, server.tls_ctx_config.session_cache_timeout, 300, INTEGER_CONFIG, NULL, updateTlsCfgInt),
    { "tls-session-cache-timeout",      &fp1 },    // tls-session-cache-timeout [number]
    // createBoolConfig("tls-cluster", NULL, MODIFIABLE_CONFIG, server.tls_cluster, 0, NULL, updateTlsCfgBool),
    { "tls-cluster",                    &fp1 },    // tls-cluster [yes/no]
    // createBoolConfig("tls-replication", NULL, MODIFIABLE_CONFIG, server.tls_replication, 0, NULL, updateTlsCfgBool),
    { "tls-replication",                &fp1 },    // tls-replication [yes/no]
    // createEnumConfig("tls-auth-clients", NULL, MODIFIABLE_CONFIG, tls_auth_clients_enum, server.tls_auth_clients, TLS_CLIENT_AUTH_YES, NULL, NULL),
    { "tls-auth-clients",               &fp1 },    // tls-auth-clients [no|yes|optional]
    // createBoolConfig("tls-prefer-server-ciphers", NULL, MODIFIABLE_CONFIG, server.tls_ctx_config.prefer_server_ciphers, 0, NULL, updateTlsCfgBool),
    { "tls-prefer-server-ciphers",      &fp1 },    // tls-prefer-server-ciphers [yes/no]
    // createBoolConfig("tls-session-caching", NULL, MODIFIABLE_CONFIG, server.tls_ctx_config.session_caching, 1, NULL, updateTlsCfgBool),
    { "tls-session-caching",            &fp1 },    // tls-session-caching [yes/no]
    // createStringConfig("tls-cert-file", NULL, MODIFIABLE_CONFIG, EMPTY_STRING_IS_NULL, server.tls_ctx_config.cert_file, NULL, NULL, updateTlsCfg),
    { "tls-cert-file",                  &fp1 },    // tls-cert-file [path]
    // createStringConfig("tls-key-file", NULL, MODIFIABLE_CONFIG, EMPTY_STRING_IS_NULL, server.tls_ctx_config.key_file, NULL, NULL, updateTlsCfg),
    { "tls-key-file",                   &fp1 },    // tls-key-file [path]
    // createStringConfig("tls-dh-params-file", NULL, MODIFIABLE_CONFIG, EMPTY_STRING_IS_NULL, server.tls_ctx_config.dh_params_file, NULL, NULL, updateTlsCfg),
    { "tls-dh-params-file",             &fp1 },    // tls-dh-params-file [path]
    // createStringConfig("tls-ca-cert-file", NULL, MODIFIABLE_CONFIG, EMPTY_STRING_IS_NULL, server.tls_ctx_config.ca_cert_file, NULL, NULL, updateTlsCfg),
    { "tls-ca-cert-file",               &fp1 },    // tls-ca-cert-file [path]
    // createStringConfig("tls-ca-cert-dir", NULL, MODIFIABLE_CONFIG, EMPTY_STRING_IS_NULL, server.tls_ctx_config.ca_cert_dir, NULL, NULL, updateTlsCfg),
    { "tls-ca-cert-dir",                &fp1 },    // tls-ca-cert-dir [path]
    // createStringConfig("tls-protocols", NULL, MODIFIABLE_CONFIG, EMPTY_STRING_IS_NULL, server.tls_ctx_config.protocols, NULL, NULL, updateTlsCfg),
    { "tls-protocols",                  &fp1 },    // tls-protocols [string]
    // createStringConfig("tls-ciphers", NULL, MODIFIABLE_CONFIG, EMPTY_STRING_IS_NULL, server.tls_ctx_config.ciphers, NULL, NULL, updateTlsCfg),
    { "tls-ciphers",                    &fp1 },    // tls-ciphers [string]
    // createStringConfig("tls-ciphersuites", NULL, MODIFIABLE_CONFIG, EMPTY_STRING_IS_NULL, server.tls_ctx_config.ciphersuites, NULL, NULL, updateTlsCfg),
    { "tls-ciphersuites",               &fp1 },    // tls-ciphersuites [string]
#endif

    // Redis non-standard configs (ordered as they appear in config.c/loadServerConfigFromString())
    { "bind",                           &bp },     // bind [address] [address] ...
    { "unixsocketperm",                 &fp1 },    // unixsocketperm [perm]
    { "save",                           &savep },  // save [seconds] [changes] or save ""
    { cDir,                             &fp1 },    // dir [path]
    { "logfile",                        &fp1 },    // logfile [file]
    //"include" is handled in ParseConfFile()
    { "client-query-buffer-limit",      &fp1 },    // client-query-buffer-limit [number]
    { "slaveof",                        &fp2 },    // slaveof [masterip] [master port]
    { "replicaof",                      &fp2 },    // replicaof [masterip] [master port]
    { "requirepass",                    &fp1 },    // requirepass [string]
    { "list-max-ziplist-entries",       &fp1 },    // list-max-ziplist-entries [number]     DEAD OPTION
    { "list-max-ziplist-value",         &fp1 },    // list-max-ziplist-value [number]       DEAD OPTION
    { "rename-command",                 &fp2 },    // rename-command [command] [string]
    { "cluster-config-file",            &fp1 },    // cluster-config-file [filename]
    { "client-output-buffer-limit",     &fp4 },    // client-output-buffer-limit [class] [hard limit] [soft limit] [soft seconds]
    { "oom-score-adj-values",           &fp3 },    // oom-score-adj-values [number] [number] [number]
    { "notify-keyspace-events",         &fp1 },    // notify-keyspace-events [string]
    { "user",                           &userp },  // user <username> ... acl rules ...
    { "loadmodule",                     &fp1 },    // loadmodule [filename]
    { "sentinel",                       &sp  },    // sentinel commands
    { "watchdog-period",                &fp1 },    // watchdog-period [number]
    { cInclude,                         &fp1 }     // include [path]
};

std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        if (!item.empty())
            elems.push_back(item);
    }
    return elems;
}

std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, elems);
    return elems;
}

vector<string> Tokenize(string line) {
    vector<string> tokens;
    stringstream token;

    // no need to parse empty lines, or comment lines (which may have unbalanced quotes)
    if ((line.length() == 0) ||
        ((line.length() != 0) && (*line.begin()) == '#')) {
        return tokens;
    }

    for (string::const_iterator sit = line.begin(); sit != line.end(); sit++) {
        char c = *(sit);
        if (isspace(c) && token.str().length() > 0) {
            tokens.push_back(token.str());
            token.str("");
        }
        else if (c == '\'' || c == '\"') {
            char endQuote = c;
            string::const_iterator endQuoteIt = sit;
            while (++endQuoteIt != line.end()) {
                if (*endQuoteIt == endQuote) break;
            }
            if (endQuoteIt != line.end()) {
                while (++sit != endQuoteIt) {
                    token << (*sit);
                }

                // The code above strips quotes. In certain cases (save "") the quotes should be preserved around empty strings
                if (token.str().length() == 0)
                    token << endQuote << endQuote;

                // correct paths for windows nomenclature
                string path = token.str();
                replace(path.begin(), path.end(), '/', '\\');
                tokens.push_back(path);

                token.str("");
            }
            else {
                // stuff the imbalanced quote character and continue
                token << (*sit);
            }
        }
        else {
            token << c;
        }
    }
    if (token.str().length() > 0) {
        tokens.push_back(token.str());
    }

    return tokens;
}

void ParseConfFile(string confFile, string cwd, ArgumentMap& argMap) {
    ifstream config;
    string line;

    char fullConfFilePath[MAX_PATH];
    if (PathIsRelativeA(confFile.c_str())) {
        if (NULL == PathCombineA(fullConfFilePath, cwd.c_str(), confFile.c_str())) {
            throw std::system_error(GetLastError(), system_category(), "PathCombineA failed");
        }
    }
    else {
        strcpy(fullConfFilePath, confFile.c_str());
    }

    config.open(fullConfFilePath);
    if (config.fail()) {
        stringstream ss;
        ss << "Failed to open the .conf file: " << confFile << " CWD=" << cwd.c_str();
        throw invalid_argument(ss.str());
    }
    else {
        char confFileDir[MAX_PATH];
        strcpy(confFileDir, fullConfFilePath);
        if (FALSE == PathRemoveFileSpecA(confFileDir)) {
            throw std::system_error(GetLastError(), system_category(), "PathRemoveFileSpecA failed");
        }
        g_pathsAccessed.push_back(confFileDir);
    }

    while (!config.eof()) {
        getline(config, line);
        vector<string> tokens = Tokenize(line);
        if (tokens.size() > 0) {
            string parameter = tokens.at(0);
            if (parameter.at(0) == '#') {
                continue;
            }
            else if (parameter.compare(cInclude) == 0) {
                ParseConfFile(tokens.at(1), cwd, argMap);
            }
            else if (g_redisArgMap.find(parameter) == g_redisArgMap.end()) {
                stringstream err;
                err << "unknown conf file parameter : " + parameter;
                throw invalid_argument(err.str());
            }

            vector<string> params = g_redisArgMap[parameter]->Extract(tokens);
            g_argMap[parameter].push_back(params);
        }
    }
}

//TODO: check if list is complete in 6.0 (any new config options related to persistence?)
vector<string> incompatibleNoPersistenceCommands{
    "min-replicas-to-write", "min-slaves-to-write",
    "min-replicas-max-lag", "min-slaves-max-lag",
    "appendonly",
    "appendfilename",
    "appendfsync",
    "no-appendfsync-on-rewrite",
    "auto-aof-rewrite-percentage",
    "auto-aof-rewrite-min-size",
    "aof-rewrite-incremental-fsync",
    "save"
};

void ValidateCommandlineCombinations() {
    if (g_argMap.find(cPersistenceAvailable) != g_argMap.end()) {
        if (g_argMap[cPersistenceAvailable].at(0).at(0) == cNo) {
            string incompatibleCommand = "";
            for (auto command : incompatibleNoPersistenceCommands) {
                if (g_argMap.find(command) != g_argMap.end()) {
                    incompatibleCommand = command;
                    break;
                }
            }
            if (incompatibleCommand.length() > 0) {
                stringstream ss;
                ss << "'" << cPersistenceAvailable << " " << cNo << "' command not compatible with '" << incompatibleCommand << "'. Exiting.";
                throw std::invalid_argument(ss.str().c_str());
            }
        }
    }
}

void ParseCommandLineArguments(int argc, char** argv) {
    if (argc < 2) {
        return;
    }

    bool confFile = false;
    string confFilePath;
    for (int n = (confFile ? 2 : 1); n < argc; n++) {
        if (string(argv[n]).substr(0, 2) == "--") {
            string argumentString = string(argv[n]);
            string argument = argumentString.substr(2, argumentString.length() - 2);
            transform(argument.begin(), argument.end(), argument.begin(), ::tolower);

            // Some -- arguments are passed directly to redis.c::main()
            if (find(cRedisArgsForMainC.begin(), cRedisArgsForMainC.end(), argument) != cRedisArgsForMainC.end()) {
                if (strcasecmp(argument.c_str(), "test-memory") == 0) {
                    // The test-memory argument is followed by a integer value
                    n++;
                }
            }
            else {
                // -- arguments processed before calling redis.c::main()
                if (g_redisArgMap.find(argument) == g_redisArgMap.end()) {
                    stringstream err;
                    err << "unknown argument: " << argument;
                    throw invalid_argument(err.str());
                }

                vector<string> params;
                if (argument == cSentinel) {
                    try {
                        vector<string> sentinelSubCommands = g_redisArgMap[argument]->Extract(n, argc, argv);
                        for (auto p : sentinelSubCommands) {
                            params.push_back(p);
                        }
                    }
                    catch (invalid_argument iaerr) {
                        // if no subcommands could be mapped, then assume this is the parameterless --sentinel command line only argument
                    }
                }
                else if (argument == cServiceRun) {
                    // When the service starts the current directory is %systemdir%. This needs to be changed to the
                    // directory the executable is in so that the .conf file can be loaded.
                    char szFilePath[MAX_PATH];
                    if (GetModuleFileNameA(NULL, szFilePath, MAX_PATH) == 0) {
                        throw std::system_error(GetLastError(), system_category(), "ParseCommandLineArguments: GetModuleFileName failed");
                    }
                    string currentDir = szFilePath;
                    auto pos = currentDir.rfind("\\");
                    currentDir.erase(pos);

                    if (FALSE == SetCurrentDirectoryA(currentDir.c_str())) {
                        throw std::system_error(GetLastError(), system_category(), "SetCurrentDirectory failed");
                    }
                }
                else {
                    params = g_redisArgMap[argument]->Extract(n, argc, argv);
                }
                g_argMap[argument].push_back(params);
                n += (int) params.size();
            }
        }
        else if (string(argv[n]).substr(0, 1) == "-") {
            // Do nothing, the - arguments are passed to redis.c::main() as they are
        }
        else {
            confFile = true;
            confFilePath = argv[n];
        }
    }

    char cwd[MAX_PATH];
    if (0 == ::GetCurrentDirectoryA(MAX_PATH, cwd)) {
        throw std::system_error(GetLastError(), system_category(), "ParseCommandLineArguments: GetCurrentDirectoryA failed");
    }

    if (confFile) {
        ParseConfFile(confFilePath, cwd, g_argMap);
    }

    // grab directory where RDB/AOF files will be created so that service install can add access allowed ACE to path
    string fileCreationDirectory = ".\\";
    if (g_argMap.find(cDir) != g_argMap.end()) {
        fileCreationDirectory = g_argMap[cDir][0][0];
        replace(fileCreationDirectory.begin(), fileCreationDirectory.end(), '/', '\\');
    }
    if (PathIsRelativeA(fileCreationDirectory.c_str())) {
        char fullPath[MAX_PATH];
        if (NULL == PathCombineA(fullPath, cwd, fileCreationDirectory.c_str())) {
            throw std::system_error(GetLastError(), system_category(), "PathCombineA failed");
        }
        fileCreationDirectory = fullPath;
    }
    g_pathsAccessed.push_back(fileCreationDirectory);

    ValidateCommandlineCombinations();
}

vector<string> GetAccessPaths() {
    return g_pathsAccessed;
}