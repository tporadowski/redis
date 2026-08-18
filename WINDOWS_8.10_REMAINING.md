# Remaining work vs official Redis 8.10.0

| Field | Value |
|-------|--------|
| **For** | `win-8.10` after M10 (10.1–10.4) |
| **Date** | 2026-08-18 |
| **Status** | 13.x–14.x done; 17.1 COUNT `long long`; 17.2 QFork block-first alloc |
| **Upstream** | Redis Open Source 8.10.0 |
| **How to run** | “Take 11.1” / “work on 12.1” means implement that PR, commit locally, **do not push** unless asked |

M0–M10 in [WINDOWS_8.10_PORT_PLAN.md](WINDOWS_8.10_PORT_PLAN.md) are **Done**. This file is the work queue for what official 8.10 still has that this port does not, or does not do the same way.

Do **not** introduce `PORT_LONG`. Do **not** start Rust modules unless asked. Do **not** force-push `win-5.0`.

---

## What is already at core parity

TCP, TLS, pathname AF_UNIX, ACL, Functions, in-tree vector-sets, Multi-Part AOF + `BACKUP`, mapped-heap QFork COW (`BGSAVE` / `BGREWRITEAOF`), cluster (MEET / slots / failover smoked), Sentinel + scripts, IO threads (including TLS + `io-threads 4`), Windows Service + Event Log, zip layout, 5.0 RDB load.

---

## Priority

13.x (fenced Tcl + leftover replica AUTH) and 14.x (protocol close, HNSW AVX2, unixsocketperm) are **Done**. 17.1 COUNT parse and 17.2 block-first QFork alloc are **Done**. Next QFork items if asked: unmap empty 4 MB blocks; `MEM_RESET` on free; jemalloc decay/purge; try `LG_PAGE` 18/20.

1. **Later / out of this queue unless asked** — Rust modules, faithful `RedisModule_Fork` stack, MSI, GA tag.

---

## Tracker

| ID | Area | Status | Notes / acceptance |
|----|------|--------|--------------------|
| 11.1 | QFork | Done | jemalloc `LG_PAGE=16`; 4 MB maps carved into 64 KB slots; `g_BypassMemoryMapOnAlloc=0` |
| 11.2 | QFork | Done | Live `--QFork` `BGSAVE`; `redis-check-rdb` OK; SET during save |
| 11.3 | QFork | Done | QFork-child `BGREWRITEAOF`; restart loads MP-AOF |
| 12.1 | Replication | Done | Diskful `PSYNC`; `SET` after full sync visible on replica |
| 12.2 | Replication | Done | Diskless `PSYNC` + `rdbchannel`; live `SET` after sync |
| 13.1 | Tests | Done | Fenced `wintest`: printver, incr, string, keyspace, expire, auth, protocol. Inventory: `tests/windows/DEFERRED-TESTS.md` |
| 13.2 | Tests | Done | AUTH replica sync, binary `MASTERAUTH` (rdbchannel yes/no), AUTH-fail `maxclients`. Handshake: `connRead` + `WSIOCP_CancelQueuedRead` (no IOCP drain). Windows test waits on replica log, not INFO poll. |
| 14.1 | Client | Done | `shutdown(SD_SEND)` + drain before `closesocket`; protocol `-ERR` / QUIT `+OK` visible. Desync flood tests still skip (raw Tcl) |
| 14.2 | Vecsets | Done | clang-cl AVX2 via `__cpuid`/`_xgetbv`; AVX-512 stays off |
| 14.3 | AF_UNIX | Done | `smoke_unix.ps1` checks socket exists + NTFS owner/DACL; `unixsocketperm` is not Linux `0700` |
| 15.1 | Product | Deferred | Tag `v8.10.0-win.1` + switch default branch — **only when asked** |
| 16.1 | Modules | Out of queue | Bundled Rust Search/JSON/TS/Bloom — next program |
| 16.2 | Modules | Out of queue | Faithful `RedisModule_Fork` stack continuation — Decision 26; `SetForkChildFn` stays the contract |
| 17.1 | SCAN | Partial | COUNT parsed as `long long` (LLP64). Isolated COUNT overflow + `{foo}-*` MATCH green. Full `unit/scan` parked (LiveKernel 141) |
| 17.2 | QFork | Done | Block-first alloc: search 4 MB blocks, then bit-run in `used[]`. No per-slot walk of the heap |

---

## 11 — Mapped-heap QFork (COW)

jemalloc 5.3 with `LG_PAGE=22` OOMed a 16-byte `zmalloc` at `aeCreate` on the mapped heap (`narenas:1` did not help). 11.1 uses `LG_PAGE=16` (64 KB) and carves those pages out of the existing 4 MB QFork maps. Parent stays on the mapped heap. 11.2/11.3 spawn a real `--QFork` child (`FILE_MAP_COPY` at the same VA). Child must not walk process-`.bss` module/function globals (NULL in the new image). Payload `CreateFileMapping` handle stays open until `waitpid` so the child can `DuplicateHandle` it. Dummy `--QForkExit` remains only when the mapped heap is off (sentinel / `persistence-available no` / check tools).

---

## 12 — Replica PSYNC

**Why official Redis is different:** A replica `REPLICAOF` / `PSYNC`s, gets an RDB (disk or socket), then live commands.

**What we did:** Handshake after ConnectEx (7.2) was stalling on a blocking `connSyncReadLine` for `$len` (IOCP). Windows now accumulates that line with `connRead`, writes leftover RDB bytes (and detects `$EOF:` on diskful load of a diskless stream). `fdapi_fstat` maps RFD→CRT; `fdapi_rename` is POSIX replace (`MoveFileEx` + short ACCESS_DENIED retry). Replica closes the transfer fd before `temp-*.rdb` → `dump.rdb` (NTFS cannot rename an open source or replace an open dest). `bg_unlink` is a plain `unlink` on Windows.

**DoD for 12.1:** Two processes, `repl-diskless-sync no` + `repl-rdb-channel no`, `REPLICAOF`, log “Finished with success”, `SET` on master visible on replica. **Met.**

**DoD for 12.2:** `repl-diskless-sync yes` (rdbchannel on and off) completes the same check, including a live `SET` after the link is up. Replica `repl-diskless-load flushdb` also loaded from the socket. **Met.** Replica AUTH / `MASTERAUTH` Tcl is in the 13.2 default run. `SYNC` stream / `attach_to_replication_stream` stay skipped.

---

## 13 — Test suite

Official 8.10 runs a large `tests/unit` + `tests/integration` set. This port has smokes plus a widening 13.1 `wintest.tcl` list.

**DoD for 13.1:** Met under fences. **13.2:** Met. AUTH replica sync, binary `MASTERAUTH`, and AUTH-fail `maxclients` are in the default fenced run. Remaining replica Tcl is the `SYNC` stream group (`DEFERRED-TESTS.md`).

**Stay skipped unless the OS grows the feature:** abstract Unix, `/proc`/`smaps`, `taskset`, `setsid`, `daemonize`, `SIGSTOP` process-state tests, gcc `.so` moduleapi.

**Deferred, not dropped:** every other skip is inventoried in [tests/windows/DEFERRED-TESTS.md](tests/windows/DEFERRED-TESTS.md). Re-run those groups when the unblock column is done. Two `0x133 DPC_WATCHDOG_VIOLATION` resets (2026-08-17 12.x, 2026-08-18 13.x probe) are why the default run now: omits AF_UNIX unless `REDIS_TEST_UNIXSOCKET=1`, caps QFork at `QFORK_HEAP_BYTES=512M`, denies `needs:repl`/`repl`/`cluster`, skips `SYNC`/long `KEYS`/`RANDOMKEY`, runs one unit per process, kills leftover `redis-server`, and expects Defender exclusions (`tests/windows/defender-exclude.ps1`).

---

## 14 — Smaller contracts

- **14.1** Met: `fdapi_close` does `shutdown(SD_SEND)` then non-blocking recv drain so Windows does not RST and drop the just-written `-ERR` / `+OK`. `unit/protocol` (minus the three desync flood cases), `unit/quit`, and unauthenticated multibulk are green. Desync tests stay on the skip-list: the client keeps writing after close and `gets` is empty.
- **14.2** Met: HNSW AVX2 on clang-cl uses `__cpuid`/`_xgetbv` (OSXSAVE + YMM). `__builtin_cpu_supports` is not used on `_WIN32`. AVX-512 stays scalar-off.
- **14.3** Met: `smoke_unix.ps1` asserts the socket file exists and prints NTFS owner/DACL. `unixsocketperm` still goes through CRT `_chmod` (owner write bit only), not POSIX `0700`.

`pthread_cancel` stays `ENOSYS` (Decision 11). BIO/IO already cooperative-join. Not a 14.x unless a real shutdown hang appears.

---

## 17 — SCAN / QFork heap

**17.1** Met: `SCAN COUNT` uses `long long` so LLP64 accepts the same values as Linux `long`. Isolated Tcl green. Full `unit/scan` stays off default `wintest` (2026-08-18 LiveKernel 141 during expire+TYPE).

**17.2** Met: `AllocHeapBlock` searches **4 MB blocks** first (skip `used == ~0` in O(1)), then a bit-run inside the block. Cross-block seam and multi-block requests stay correct. Smoke: `qfork_heap_smoke` (burst + holes + 8 MB + seam) and `mapped_jemalloc_smoke`. Next if asked: unmap empty blocks, `MEM_RESET` on free, jemalloc decay.

---

## Not this queue

| Item | Why |
|------|-----|
| Rust Search / JSON / TS / Bloom | Separate program (`modules/modules.yaml`) |
| `RedisModule_Fork` returns 0 into the module | Impossible with `CreateProcess`; Decision 26 |
| 32-bit | Locked dropped |
| MSI / Chocolatey / signing | Decision 19 |
| wepoll | Closed at 9.3 |
| `daemonize` as a real fork | Service instead |
| Official Docker / packages | Not this repo |

---

## PR titles (execution order)

| # | Title | Depends on |
|---|---------|------------|
| 11.1 | `fix: jemalloc mapped-heap OOM at aeApiCreate` | 3.3 |
| 11.2 | `port: live QFork COW BGSAVE` | 11.1 |
| 11.3 | `port: QFork child AOF rewrite` | 11.2 |
| 12.1 | `fix: replica diskful PSYNC` | 7.2 |
| 12.2 | `test: diskless PSYNC end-to-end` | 12.1, 4.2 |
| 13.1 | `test: widen wintest 8.10 units` | 10.1 |
| 13.2 | `test: replica cases off skip-list` | 12.1, 13.1 |
| 14.1 | `fix: protocol error strings to hiredis` | 10.1 |
| 14.2 | `opt: HNSW AVX probe on clang-cl` | 6.2 |
| 14.3 | `docs: unixsocketperm vs NTFS DACL` | 9.1 |
| 17.1 | `fix: SCAN COUNT long long on LLP64` | 10.4 |
| 17.2 | `perf: QFork block-first heap search` | 11.1 |
