# Remaining work vs official Redis 8.10.0

| Field | Value |
|-------|--------|
| **For** | `win-8.10` after M10 (10.1–10.4) |
| **Date** | 2026-08-17 |
| **Status** | Queue — not started |
| **Upstream** | Redis Open Source 8.10.0 |
| **How to run** | “Take 11.1” / “work on 12.1” means implement that PR, commit locally, **do not push** unless asked |

M0–M10 in [WINDOWS_8.10_PORT_PLAN.md](WINDOWS_8.10_PORT_PLAN.md) are **Done**. This file is the work queue for what official 8.10 still has that this port does not, or does not do the same way.

Do **not** introduce `PORT_LONG`. Do **not** start Rust modules unless asked. Do **not** force-push `win-5.0`.

---

## What is already at core parity

TCP, TLS, pathname AF_UNIX, ACL, Functions, in-tree vector-sets, Multi-Part AOF + `BACKUP`, mapped-heap QFork COW (`BGSAVE` / `BGREWRITEAOF`), cluster (MEET / slots / failover smoked), Sentinel + scripts, IO threads (including TLS + `io-threads 4`), Windows Service + Event Log, zip layout, 5.0 RDB load.

---

## Priority

1. **Replica PSYNC end-to-end** (12.x) — without this, replication is not proven.
3. **Test suite** (13.x) — widen 10.1 so regressions stay caught.
4. **Smaller Windows contracts** (14.x) — protocol text, HNSW AVX, `unixsocketperm` docs/tests.
5. **Later / out of this queue unless asked** — Rust modules, faithful `RedisModule_Fork` stack, MSI, GA tag.

---

## Tracker

| ID | Area | Status | Notes / acceptance |
|----|------|--------|--------------------|
| 11.1 | QFork | Done | jemalloc `LG_PAGE=16`; 4 MB maps carved into 64 KB slots; `g_BypassMemoryMapOnAlloc=0` |
| 11.2 | QFork | Done | Live `--QFork` `BGSAVE`; `redis-check-rdb` OK; SET during save |
| 11.3 | QFork | Done | QFork-child `BGREWRITEAOF`; restart loads MP-AOF |
| 12.1 | Replication | Not started | Replica `PSYNC` + `SET` after full sync (diskful first) |
| 12.2 | Replication | Not started | Diskless `PSYNC` / `rdbchannel` end-to-end (4.2 path was wired, not re-verified) |
| 13.1 | Tests | Not started | Add passing 8.10 units to `wintest.tcl` (string minus skip-list, keyspace, expire, …) |
| 13.2 | Tests | Not started | Replica Tcl cases off the skip-list once 12.1 is green |
| 14.1 | Client | Not started | Protocol-error replies: hiredis should see the server error string, not only `I/O error reading reply` |
| 14.2 | Vecsets | Not started | Optional: HNSW AVX on Windows if a clang-cl-safe CPU probe exists; else leave scalar |
| 14.3 | AF_UNIX | Not started | Test/document NTFS DACL vs `unixsocketperm` (best-effort `_chmod`) |
| 15.1 | Product | Deferred | Tag `v8.10.0-win.1` + switch default branch — **only when asked** |
| 16.1 | Modules | Out of queue | Bundled Rust Search/JSON/TS/Bloom — next program |
| 16.2 | Modules | Out of queue | Faithful `RedisModule_Fork` stack continuation — Decision 26; `SetForkChildFn` stays the contract |

---

## 11 — Mapped-heap QFork (COW)

jemalloc 5.3 with `LG_PAGE=22` OOMed a 16-byte `zmalloc` at `aeCreate` on the mapped heap (`narenas:1` did not help). 11.1 uses `LG_PAGE=16` (64 KB) and carves those pages out of the existing 4 MB QFork maps. Parent stays on the mapped heap. 11.2/11.3 spawn a real `--QFork` child (`FILE_MAP_COPY` at the same VA). Child must not walk process-`.bss` module/function globals (NULL in the new image). Payload `CreateFileMapping` handle stays open until `waitpid` so the child can `DuplicateHandle` it. Dummy `--QForkExit` remains only when the mapped heap is off (sentinel / `persistence-available no` / check tools).

---

## 12 — Replica PSYNC

**Why official Redis is different:** A replica `REPLICAOF` / `PSYNC`s, gets an RDB (disk or socket), then live commands.

**What we know:** Outgoing connect is associate + ConnectEx (7.2). Diskless `do_rdbSaveToSockets` + `WSADuplicateSocket` is wired (4.2). Replica handshake still stalled after “Master replied to PING” last time it was fully tried. 10.1 skip-list still excludes replica Tcl.

**DoD for 12.1:** Two processes, `REPLICAOF`, `WAIT` or log “Finished with success”, `SET` on master visible on replica. Prefer diskful first.

**DoD for 12.2:** `repl-diskless-sync yes` (and rdbchannel if still on) completes the same check.

---

## 13 — Test suite

Official 8.10 runs a large `tests/unit` + `tests/integration` set. This port has smokes plus `unit/printver` + `unit/type/incr`.

**DoD for 13.1:** `runtest-win.ps1` includes more units that already pass (or pass after small harness fixes). Do not drop replica cases from the skip-list until 12.1.

**Stay skipped unless the OS grows the feature:** abstract Unix, `/proc`/`smaps`, `taskset`, `setsid`, `daemonize`, `SIGSTOP` process-state tests, gcc `.so` moduleapi.

---

## 14 — Smaller contracts

- **14.1** Protocol tests expect `*invalid multibulk length*` etc. hiredis on Windows often reports `I/O error reading reply` (connection reset vs reading the `-ERR`). Fix so the client sees the server’s error when it was sent.
- **14.2** Vector-sets use the scalar HNSW path (`__builtin_cpu_supports` / `__cpu_model` missing in clang-cl). Optional speed-up only.
- **14.3** `unixsocketperm` is not Linux mode bits. Document + a smoke that the socket file exists and a second user/ACL story is NTFS, not `0700`.

`pthread_cancel` stays `ENOSYS` (Decision 11). BIO/IO already cooperative-join. Not a 14.x unless a real shutdown hang appears.

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
