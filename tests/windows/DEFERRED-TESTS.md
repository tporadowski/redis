# Windows Tcl suite — what is skipped

Default run: `tests/windows/runtest-win.ps1`.

`tests/windows/wintest.tcl` lists every `tests/unit`, `tests/unit/type`, and
`tests/unit/cluster` stem, plus `windows/type_list_nb`, `windows/blpop_one`,
`windows/type_set_nb`, `windows/type_zset_nb`, `windows/type_stream_nb`,
`windows/iocp`, `windows/aof`, `windows/regression`, and these integration
stems: `integration/block-repl`, `integration/psync2-reg`,
`integration/replication-2`, `integration/replication-3`,
`integration/replication-psync`, `integration/redis-benchmark`,
`integration/psync2-master-restart`, `integration/shutdown`,
`integration/psync2-pingoff`, `integration/failover`,
`integration/replication-4`, `integration/dismiss-mem`,
`integration/corrupt-dump-fuzzer`, `integration/psync2`,
`integration/replication-iothreads`, `integration/replication-buffer`,
`integration/rdb`, `integration/replication-rdbchannel`, `integration/aof`,
`integration/redis-cli`, `integration/corrupt-dump`,
`integration/aof-multi-part`, `integration/replication`,
`integration/convert-zipmap-hash-on-load`,
`integration/convert-ziplist-hash-on-load`,
`integration/convert-ziplist-zset-on-load`, `integration/logging`,
`integration/aof-race`, and `integration/backup`.

`tests/unit/moduleapi` is not on that list. Those files load
`tests/modules/*.so`, and this CMake build does not build those test modules
as DLLs.

The runner does not deny `needs:repl`, `repl`, or `cluster`. Replication,
cluster, blocking list/set/zset pops, SCAN, KEYS, RANDOMKEY, SORT, MIGRATE,
FLUSH/SWAPDB, query-buffer shrink, scripting and function kill, MONITOR, and
extra-client cases are executed.

`tests/windows/skip-list.txt` matches only facilities Windows does not
provide:

- process stop/continue expressed as SIGSTOP/SIGCONT (`pause process`,
  `resume process`, stopped clients, `DEBUG SLEEP` while stopped)
- abstract-namespace Unix sockets
- `/proc` (`smaps`, `oom_score_adj` when the test file itself has not already
  taken the non-Linux branch)
- `taskset`, `setsid`, `daemonize`

`pause_process` / `resume_process` in `tests/support/util.tcl` suspend and
resume through `redis-test-launcher.exe` on Windows. Tests that call those
procs run; they are not on the skip list.

Ignores that upstream `./runtest` also makes without extra flags stay in
effect: the `large-memory` tag when `--large-memory` is not passed, bodies in
`unit/tls.tcl` (and other files) behind `if {$::tls}` when `--tls` is not
passed, the `if 0` block in `unit/gcra.tcl`, and branches the test file
already takes only when `uname` is Linux or `/proc` exists.

A test that needs an AF_UNIX listener is run with Unix listen on for that
test (`REDIS_TEST_UNIXSOCKET=1` or an explicit `unixsocket`). `QFORK_HEAP_BYTES`
is raised for a unit when a correct workload does not fit the default cap.

## History

Earlier passes on this host bugchecked during blocking-pop storms, SCAN
write-load, and long KEYS patterns (`0x133` DPC_WATCHDOG_VIOLATION,
LiveKernel 141, Kernel-Power 41). Those cases are in the default run. A
reboot is not a reason to put them back on the skip list.
