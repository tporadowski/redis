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
  `runtest-win.ps1` now refuses `unit/type/list`, `set`, `zset`, `stream`,
  `scan`, `sort`, `multi`, `pubsub` unless `REDIS_TEST_UNSAFE=1`.

Fences: no default AF_UNIX listen, `QFORK_HEAP_BYTES=512M`,
`--tags -needs:repl -repl -cluster`, this skip-list, one unit per `tclsh`,
kill leftover `redis-server` between units. Do **not** put `unit/scan` on
the default `wintest` list until expire+TYPE / write-load are fenced.
COUNT overflow + `{foo}-*` MATCH are green in isolation (17.1 COUNT is
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
| **13.2** `MASTERAUTH` binary password (rdbchannel yes/no) | default `unit/auth` | green (IOCP handshake + log wait) |
| `attach_to_replication_stream` / `SYNC` (`needs:repl`, `repl`, INCREX rewrite, three `needs:debug` names) | `--tags` + skip-list | extra-client AcceptEx + QFork `SYNC` without hang |
| Protocol desync flood #1–#3 | default `unit/protocol` | green (non-blocking Tcl read) |
| Large payload / 10k SET / BITOP+GEO+BITPOS fuzz / AVX-512 BITOP | skip-list | timed solo run; mapped-heap FLUSHALL inside BITOP fuzz drops the client |
| `GETEX PXAT option` | default `unit/type/string` | green (server `TIME` timestamp) |
| SWAPDB / FLUSHALL coverage + MULTI WATCH+FLUSH/SWAP | skip-list | faster FLUSHALL on mapped heap |
| HINCRBYFLOAT 1.23 pretty-print | test gate (Windows `long double` is 64-bit) | 80-bit `long double` (Linux x86_64 only) |
| `unit/sort` (10k hash-table SORT + issue #19 floats + EVAL SORT) | not in default `wintest.tcl` | faster SORT BY; scripting write flags |
| `unit/multi` (script timeout + remaining after WATCH) | not in default `wintest.tcl` | Lua `lua-time-limit` abort on Windows |
| `unit/pubsub` | default `wintest.tcl` | green after write rearm; EVAL-write “publish to self inside script” skipped |
| `unit/type/list` (BLPOP/BLMPOP extra-client) | skip-list + runner deny | **2026-08-22 reboot.** One-shot AcceptEx is in; `windows/blpop_one` (1–2 extra clients) is green. Do not `-Single` the full official unit |
| `unit/type/set`, `zset`, `stream` | runner deny (`REDIS_TEST_UNSAFE`) | same watchdog class as list; not started |
| `unit/scan` (whole unit) | not in default `wintest.tcl` | timed solo **without** expire+TYPE / write-load / #4906; 2026-08-18 22:23 LiveKernel 141 during full unit |
| `unit/quit` | default `wintest.tcl` (14.1) | green |
| `SCAN COUNT overflow` / `{foo}-*` MATCH | green in isolation (not default list) | COUNT is `long long` (17.1). Full `unit/scan` still parked |
| `RANDOMKEY` + long `KEYS` globs | skip-list | timed solo run after fences stay green |
| `unit/acl-v2` BITFIELD selector sweep | not in default `wintest` | server dropped after ~8 min of increasingly slow BITFIELD ACL cases |
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
`unit/keyspace`, `unit/expire`, `unit/auth`, `unit/protocol`, `unit/quit`,
`unit/limits`, `unit/pubsub`, `unit/introspection`,
`unit/bitops`, `unit/bitfield`, `unit/geo`, `unit/hyperloglog`, `unit/slowlog`,
`unit/info-command`, `unit/latency-monitor`, `unit/introspection-2`,
`unit/hotkeys`, `unit/dump`, `unit/replybufsize`, `unit/querybuf`,
`unit/functions`,
`integration/convert-zipmap-hash-on-load`,
`integration/convert-ziplist-hash-on-load`,
`integration/convert-ziplist-zset-on-load`,
`windows/regression`.
`unit/scan` hung mid-unit (TUI died).
More 8.10 units are added to `wintest.tcl` as they pass under the fences.
