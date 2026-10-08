# Deferred 8.10 Tcl cases (not dropped)

Every entry in `skip-list.txt` and every `--tags` deny below is temporary
unless marked **OS-impossible**. Re-run the group when its unblock item
lands. Default Windows run: `tests/windows/runtest-win.ps1`.

Hard resets while `redis-server.exe` was under this suite:

- 2026-08-17 ~21:42 (12.x) and 2026-08-18 ~08:10 (13.x): `0x133`
  `DPC_WATCHDOG_VIOLATION` (no dump saved).
- 2026-08-18 ~22:23 during a solo `unit/scan` (after the 75s
  `{standalone} SCAN with expired keys with TYPE filter and PATTERN filter`,
  before write-load finished): LiveKernelEvent **141**
  (`VIDEO_ENGINE_TIMEOUT_DETECTED`) + **193**, then Kernel-Power 41 reboot
  at 22:26. WER pointed at
  `C:\WINDOWS\LiveKernelReports\WATCHDOG\WATCHDOG-20260818-2223.dmp`;
  that file is not on disk after reboot (Minidump empty, no `MEMORY.DMP`,
  no Event 1001 bugcheck dump). `CrashControl\AutoReboot` is still **1**;
  `CrashDumpEnabled=3`. Defender exclude + AutoReboot=0 need an **elevated**
  PowerShell — this session got HRESULT 0xc0000142.
- 2026-08-22 during a solo `unit/type/list` after an experimental AcceptEx
  re-arm: BLPOP/BLMPOP extra-client cases were progressing (`BLMPOP_LEFT:
  single existing list - quicklist` had already run ~107s) then the machine
  rebooted (Kernel-Power 41 class). QFORK_HEAP_BYTES=512M, no AF_UNIX, and
  the existing skip-list were already on. The AcceptEx re-arm is **reverted**.
  `runtest-win.ps1` no longer refuses a unit by name. `unit/scan`,
  `unit/multi`, `unit/sort`, `unit/type/list`, `unit/type/set`,
  `unit/type/zset`, and `unit/type/stream` are on the default list with
  their heavy cases still in the skip-list. `unit/pubsub` is on the
  default list.

Fences: no default AF_UNIX listen, `QFORK_HEAP_BYTES=512M`,
`--tags -needs:repl -repl -cluster`, this skip-list, one unit per `tclsh`,
kill leftover `redis-server` between units. `unit/scan` is on the default
list with expire+TYPE, write-load, and issue #4906 still skipped.
COUNT overflow + `{foo}-*` MATCH are in that fenced run (17.1 COUNT is
`long long`).

## How to re-enable a group

1. Remove the names/regexes from `skip-list.txt` (and drop the matching
   `--tags` deny in `wintest.tcl` if that is what hid the group).
2. Run **that unit only**:
   `powershell -NoProfile -ExecutionPolicy Bypass -File tests/windows/runtest-win.ps1 -Single unit/keyspace`
3. Keep `QFORK_HEAP_BYTES` and leftover-kill. Do not batch KEYS globs
   with SYNC tests until those two are green alone.

`REDIS_TEST_UNIXSOCKET=1` turns AF_UNIX listen back on for Tcl-spawned
servers. `smoke_unix.ps1` already sets `unixsocket` itself.

## Groups

| Group | Where skipped | Unblock |
|-------|---------------|---------|
| OS-impossible: `SIGSTOP`, abstract Unix, `/proc`/`smaps`, `taskset`, `setsid`, `daemonize` | skip-list | OS feature |
| gcc `.so` moduleapi | not in `wintest.tcl` `--single` list | clang-cl `.dll` moduleapi suite |
| **13.2** `windows/regression` AUTH replica + AUTH-fail `maxclients` | default `wintest.tcl` | green |
| `windows/iocp`, `windows/aof` | default `wintest.tcl` | green (IOCP one-shot, QFork AOF child reap, RDB-channel AUTH sync) |
| **13.2** `MASTERAUTH` binary password (rdbchannel yes/no) | default `unit/auth` | green (IOCP handshake + log wait) |
| `attach_to_replication_stream` / `SYNC` (`needs:repl`, `repl`, INCREX rewrite, three `needs:debug` names) | `--tags` + skip-list | extra-client AcceptEx + QFork `SYNC` without hang |
| Protocol desync flood #1–#3 | default `unit/protocol` | green (non-blocking Tcl read) |
| Large payload / 10k SET / BITOP+GEO+BITPOS fuzz / AVX-512 BITOP | skip-list | timed solo run; mapped-heap FLUSHALL inside BITOP fuzz drops the client |
| `GETEX PXAT option` | default `unit/type/string` | green (server `TIME` timestamp) |
| SWAPDB / FLUSHALL coverage + MULTI WATCH+FLUSH/SWAP | skip-list | faster FLUSHALL on mapped heap |
| HINCRBYFLOAT 1.23 pretty-print | test gate (Windows `long double` is 64-bit) | 80-bit `long double` (Linux x86_64 only) |
| `unit/sort` | default `wintest.tcl` | green (listpack/quicklist/intset/hashtable SORT and SORT_RO, issue #19 floats, 100-element speed; about 23 seconds). Still skipped: 10k quicklist and hash-table SORT BY, and SORT from scripts. Cluster server denied |
| `unit/multi` | default `wintest.tcl` | green (WATCH, MULTI/EXEC, OOM, `BGREWRITEAOF`, AOF `FLUSHALL`; about 3 seconds). Still skipped: watched-key `FLUSHALL`/`FLUSHDB`/`SWAPDB` (mapped heap) and the four `lua-time-limit` busy loops. `needs:repl` propagation stays denied |
| `unit/pubsub` | default `wintest.tcl` | green after write rearm; EVAL-write “publish to self inside script” skipped |
| `unit/type/list` | default `wintest.tcl` | green (about 8 seconds) with the 2026-08-22 storm still skipped: `BLPOP:` / `BLMPOP_` / `BRPOP:` / `BRPOPLPUSH`, plain-node `DEBUG RELOAD`, and the 4GB cases. Also still skipped: four-waiter `BLMPOP` and three-waiter nested unblock. One- and two-waiter cases ran, including `BLMOVE` and `CLIENT NO-TOUCH` |
| `unit/type/set` | default `wintest.tcl` | green (about 29 seconds). Still skipped: `SRANDMEMBER` long chain (100k members and `BGSAVE` with `rdb-key-save-delay` at `INT_MAX`). The 4GB `SADD` stays ignored without `--large-memory`. `needs:repl` propagation stayed denied |
| `unit/type/zset` | default `wintest.tcl` | green (about 59 seconds), including one- and two-waiter `BZPOP`/`BZMPOP`. Still skipped: four-waiter `BZMPOP`. `needs:repl` propagation stayed denied |
| `unit/type/stream` | default `wintest.tcl` | green (about 59 seconds), including blocking `XREAD`, `XDEL`/`XRANGE` fuzz, 10k `XADD`, and `DEBUG LOADAOF`. No extra skip. The `repl` diskless pair stayed denied |
| `unit/type/array` | default `wintest.tcl` | green (143 tests, about 13 seconds, no server change). Sparse and dense slices, superdir, rings, `ARDELRANGE`, RDB reload, AOF rewrite, and the 32-bit RDB fixture. No extra skip |
| `unit/scripting` | default `wintest.tcl` | green (about 74 seconds), both function and eval passes, including `SCRIPT KILL`, `SHUTDOWN NOSAVE` of a timed-out script, `os.clock`, 50k script GC, and the no-writes replica shebang. `cjson` is a global only when the Lua library is built with `ENABLE_CJSON_GLOBAL` (the Unix makefile flag; CMake was missing it). Still ignored: the `large-memory` server (2GB JSON and the 1GB parsers). Still denied: `repl` servers and the `needs:repl` propagation cases. The sort.tcl `SORT` from scripts name stays in the skip-list |
| `unit/scan` | default `wintest.tcl` | green without the three bombs (23 standalone tests, about 2 seconds; cluster server denied). Still skipped: TYPE+PATTERN expire scan, write-load guarantees, issue #4906 (2026-08-18 LiveKernel 141) |
| `unit/quit` | default `wintest.tcl` (14.1) | green |
| `unit/shutdown` | default `wintest.tcl` | green (QFork SIGUSR1 abort, share-delete temp RDB, `kill.exe` signal pipe) |
| `unit/aofrw` | default `wintest.tcl` | green (`DEBUG LOADAOF` no longer closes a socket whose fd number matches the AOF file) |
| `unit/lazyfree` | default `wintest.tcl` | green (UNLINK, async FLUSHDB/FLUSHALL, stream lazy free, REPLICAOF unblock; no Windows change) |
| `unit/pause` | default `wintest.tcl` | green (CLIENT PAUSE). `/OPT:ICF` folded `evalRoCommand` into `evalCommand`, so shebang `EVAL` was rejected as `EVAL_RO`; read-only now follows `CMD_READONLY`. The `needs:repl` replica-pause pair stays denied |
| `unit/other` | default `wintest.tcl` | green (save, AOF reload, BGSAVE kill, cluster-compatibility sampling). Jemalloc check expects the Windows conf (1 arena, tcache cap 8, immediate decay). `start_cluster` resizing stays denied by `-cluster` |
| `unit/obuf-limits` | default `wintest.tcl` | green (hard and soft client output buffer limits, including mid-command `HRANDFIELD` and `KEYS`; no Windows change) |
| `unit/pubsubshard` | default `wintest.tcl` | green (`SPUBLISH`/`SSUBSCRIBE` and shard messages over a replica; no Windows change) |
| `unit/client-eviction` | default `wintest.tcl` | green (`maxmemory-clients` eviction by argv, query buffer, watch, pubsub, tracking, and output buffer; no Windows change). The output-buffer case took ~71s |
| `unit/acl` | default `wintest.tcl` | green. ACL-killed subscribers are freed once the final reply is flushed, not held 100ms. Startup duplicate-user check uses `redis_server_bin`. The `repl` server stays denied |
| `unit/tracking` | default `wintest.tcl` | green (`CLIENT TRACKING`, BCAST, NOLOOP, OPTIN/OPTOUT, RESP3 invalidation, tracking-table eviction, ACL flush of pending keys; no Windows change). The three `needs:debug` cases ran |
| `unit/wait` | default `wintest.tcl` | green (`WAIT`/`WAITAOF`, replica suspend via the test launcher, postponed AOFRW, and the failover-tagged trio; no Windows change). These servers are not tagged `needs:repl`, so the replica pairs actually ran |
| `integration/logging` | default `wintest.tcl` | green (`DEBUG SEGFAULT` logs `--- STACK TRACE`). External `SIGABRT` and `SIGALRM` are skipped: the kill shim only delivers SIGINT and SIGTERM. Watchdog, `DEBUG ASSERT`, and hide-user-data stay off because `system_backtrace_supported` is 0 on Windows. No server change |
| `integration/aof-race` | default `wintest.tcl` | green (20 TCP clients, `foo` == 20000 live and after AOF reload). `redis-benchmark` adopts hiredis's SOCKET into the RFD map and registers that RFD with the event loop; hiredis still sends on the SOCKET. No server change |
| `unit/info-keysizes` | default `wintest.tcl` | green (`INFO keysizes` for string, list, set, zset, hash, UNLINK, RDB reload, and key-memory histograms; no Windows change). The `needs:repl` replica pair and both cluster servers stayed denied. The `needs:debug` cases ran |
| `unit/info` | default `wintest.tcl` | green (latency and error stats, eventloop and client metrics, `active_clients` with `io-threads 4`, and memory overhead while rehashing; no Windows change). The cluster server stayed denied. 30 tests, about 5 seconds |
| `unit/networking` | default `wintest.tcl` | green (`CONFIG SET` port and bind, empty bind without an AF_UNIX listener, `io-threads 2` prefetch while the process is suspended, idle timeout, and the pending-command pool; no Windows change). `bind-source-addr` runs only when `uname` is Linux. Protected mode looks up a non-loopback address with `hostname -I`, which fails here, so that body does not run. 13 tests, about 14 seconds |
| `integration/backup` | default `wintest.tcl` | green (BACKUP lifecycle, preload of RDB/AOF/manifest, and AOFRW overlap; 29 tests). Startup checks use `redis_server_bin`. Same-path preload compares `'/'` and `'\'` as one file. `getFilePath` splits on both separators, so a backslash manifest is not sized as a wrapped pointer difference |
| `SCAN COUNT overflow` / `{foo}-*` MATCH | default `unit/scan` | COUNT is `long long` (17.1). TYPE+PATTERN expire, write-load, and #4906 stay skipped |
| `RANDOMKEY` + long `KEYS` globs | skip-list | timed solo run after fences stay green |
| `unit/acl-v2` | default `wintest.tcl` | green (about 2 seconds, no server change). Selectors, `%R`/`%W`/`%RW` on SET and BITFIELD, ACL LOG, DRYRUN, keyspecs for MIGRATE/SORT/GEORADIUS/XREADGROUP, and ACL-file load. The old note about an 8-minute BITFIELD sweep does not match this file: the three BITFIELD cases are a few commands each and finished in a few milliseconds |
| `unit/limits` maxclients refuse | default `wintest` | green (`rejectConnection` + delayed close) |
| `unit/introspection` (full) | default `wintest.tcl` | maxAGE green. Still skipped: bgsave kill, config-during-loading, io-threads 2/4 start hang, EVAL/FUNCTION MONITOR writes |
| `unit/dump` MIGRATE | skip-list; DUMP/RESTORE is default | `--tags -repl` skips the second server but the outer test still 40k-RPUSH + mapped-heap FLUSHDB (I/O error) |
| `unit/functions` kill / load-timeout / `debug loadaof` | skip-list; rest is default | no SIGALRM Lua abort; dummy-slave `debug loadaof` dropped the client |
| `unit/querybuf` peak-shrink + fat argv | skip-list; idle/reusable cases are default | mapped-heap / clientsCron peak reset does not shrink |
| Cluster Tcl | `--tags -cluster` | dedicated cluster runner |
| AF_UNIX on every unit server | `server.tcl` default off | `REDIS_TEST_UNIXSOCKET=1`; 14.3 smoke is `smoke_unix.ps1` |

## 18.1 default units

`unit/printver`, `unit/type/incr`, `unit/type/string`, `unit/type/increx`,
`unit/type/hash`, `unit/type/list-2`, `unit/type/list-3`, `unit/type/list-4`,
`windows/type_list_nb`, `windows/type_set_nb`, `windows/type_zset_nb`,
`windows/type_stream_nb`,
`unit/keyspace`, `unit/expire`, `unit/auth`, `unit/protocol`, `unit/quit`, `unit/shutdown`,
`unit/limits`, `unit/pubsub`, `unit/introspection`,
`unit/bitops`, `unit/bitfield`, `unit/geo`, `unit/hyperloglog`, `unit/slowlog`,
`unit/info-command`, `unit/latency-monitor`, `unit/introspection-2`,
`unit/hotkeys`, `unit/dump`, `unit/replybufsize`, `unit/querybuf`,
`unit/functions`, `unit/aofrw`, `unit/lazyfree`, `unit/pause`, `unit/other`, `unit/obuf-limits`, `unit/pubsubshard`, `unit/client-eviction`, `unit/acl`, `unit/tracking`, `unit/wait`, `unit/info-keysizes`, `unit/info`, `unit/networking`, `unit/scan`, `unit/multi`, `unit/sort`, `unit/type/list`, `unit/type/set`, `unit/type/zset`, `unit/type/stream`, `unit/type/array`, `unit/scripting`, `unit/acl-v2`,
`integration/convert-zipmap-hash-on-load`,
`integration/convert-ziplist-hash-on-load`,
`integration/convert-ziplist-zset-on-load`,
`integration/logging`, `integration/aof-race`, `integration/backup`,
`windows/iocp`, `windows/aof`, `windows/regression`.
More 8.10 units are added to `wintest.tcl` as they pass under the fences.
