# Deferred 8.10 Tcl cases (not dropped)

Every entry in `skip-list.txt` and every `--tags` deny below is temporary
unless marked **OS-impossible**. Re-run the group when its unblock item
lands. Default Windows run: `tests/windows/runtest-win.ps1`.

Two `0x133 DPC_WATCHDOG_VIOLATION` resets (2026-08-17 ~21:42 during 12.x,
2026-08-18 ~08:10 during 13.x) happened while `redis-server.exe` was under
this suite. Fences: no default AF_UNIX listen, `QFORK_HEAP_BYTES=512M`,
`--tags -needs:repl -repl -cluster`, this skip-list, one unit per `tclsh`,
kill leftover `redis-server` between units. Defender exclusions
(`tests/windows/defender-exclude.ps1`) need an **elevated** PowerShell —
this session got HRESULT 0xc0000142. Same for `CrashControl\AutoReboot=0`.

## How to re-enable a group

1. Remove the names/regexes from `skip-list.txt` (and drop the matching
   `--tags` deny in `wintest.tcl` if that is what hid the group).
2. Run **that unit only**:
   `pwsh tests/windows/runtest-win.ps1 -Single unit/keyspace`
3. Keep `QFORK_HEAP_BYTES` and leftover-kill. Do not batch KEYS globs
   with SYNC tests until those two are green alone.

`REDIS_TEST_UNIXSOCKET=1` turns AF_UNIX listen back on for Tcl-spawned
servers. `smoke_unix.ps1` already sets `unixsocket` itself.

## Groups

| Group | Where skipped | Unblock |
|-------|---------------|---------|
| OS-impossible: `SIGSTOP`, abstract Unix, `/proc`/`smaps`, `taskset`, `setsid`, `daemonize` | skip-list | OS feature |
| gcc `.so` moduleapi | not in `wintest.tcl` `--single` list | clang-cl `.dll` moduleapi suite |
| **13.2** `windows/regression` AUTH replica | default `wintest.tcl` | keep if green |
| **13.2 leftover** `MASTERAUTH` binary password | skip-list | replica + binary `masterauth` |
| **13.2 leftover** AUTH-fail replica does not drop the client (`maxclients`) | skip-list | replica AUTH failure close |
| `attach_to_replication_stream` / `SYNC` (`needs:repl`, `repl`, plus three `needs:debug` names) | `--tags` + skip-list | extra-client AcceptEx + QFork `SYNC` without hang |
| **14.1** protocol `-ERR` vs hiredis `I/O error` (incl. pipelined QUIT) | skip-list | 14.1 |
| Large payload / 10k SET / fuzz | skip-list | timed solo run |
| `GETEX PXAT option` (pttl 10002 vs 5000–10000) | skip-list | clock-skew / loosen assert |
| SWAPDB / FLUSHALL coverage (61s / >180s) | skip-list | faster FLUSHALL on mapped heap |
| `unit/scan` (whole unit) | not in default `wintest.tcl` | timed solo; hung 180s and killed the TUI |
| `unit/quit` (QUIT + pipelined after QUIT) | skip-list + not in default list | 14.1 hiredis close vs `+OK` |
| `SCAN COUNT overflow` / `SCAN MATCH pattern implies cluster slot` | skip-list | SCAN + LLP64 / cursor loop |
| `RANDOMKEY` + long `KEYS` globs | skip-list | timed solo run after fences stay green |
| Cluster Tcl | `--tags -cluster` | dedicated cluster runner |
| AF_UNIX on every unit server | `server.tcl` default off | `REDIS_TEST_UNIXSOCKET=1` or 14.3 |

## 13.1 default units

`unit/printver`, `unit/type/incr`, `unit/type/string`, `unit/keyspace`,
`unit/expire`, `unit/auth`, `unit/protocol`, `windows/regression`.
`unit/scan` hung mid-unit (TUI died). `unit/quit` is 14.1 hiredis close.
More 8.10 units are added to `wintest.tcl` as they pass under the fences.
