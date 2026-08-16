# Redis 8.10 on Windows

Unofficial native port of Redis Open Source 8.10.0 (`win-8.10` on
[tporadowski/redis](https://github.com/tporadowski/redis)). This is a
**modified** version of Redis. It is **not affiliated with, endorsed by, or
sponsored by Redis Ltd.**

Need BSD Redis 5 for Windows? Use branch
[`win-5.0`](https://github.com/tporadowski/redis/tree/win-5.0) / tag
`v5.0.14.1`.

This note is the Windows operator guide. The full 8.10 option list is in
[`redis.conf`](redis.conf). Windows overlays:
[`redis.windows.conf`](redis.windows.conf) (console) and
[`redis.windows-service.conf`](redis.windows-service.conf) (SCM). Service CLI:
[`Windows Service Documentation.md`](Windows%20Service%20Documentation.md).

## Requirements

- **OS:** Windows 10 version 1803 or Windows Server 2019 and later (x64).
  AF_UNIX needs `afunix.sys` (Microsoft platform claim; not re-tested here).
- **CPU:** x64 only. 32-bit is dropped.
- **Build:** clang-cl + CMake + Ninja + VS 2022+ SDK. See
  [`scripts/build-win.ps1`](scripts/build-win.ps1).
- **License:** RSALv2 / SSPLv1 / AGPLv3 (`LICENSE.txt`). 5.0 remains BSD.

## Quick start

```
scripts\build-win.ps1
build\redis-server.exe redis.windows.conf
build\redis-cli.exe PING
```

`daemonize yes` is a **no-op** on Windows (a warning is logged). Run in a
console, or install a Windows Service (`--service-install`). Do not expect
`fork()` + `setsid()`.

## How the port differs from Linux

| Area | Windows 8.10 |
|------|----------------|
| Networking | IOCP (`ae_wsiocp`), one completion port per `aeEventLoop` |
| FDs | FDAPI + RFDMap (sockets, CRT files, pipes) |
| Persistence snapshot | QFork: pagefile-backed copy-on-write, not POSIX `fork()` |
| Logging | `--syslog-enabled` / `syslog-ident` write the **Event Log** |
| Service | `--service-install/start/stop/uninstall/name/run` |
| Paths | Windows paths (`C:\…`, backslash). `unixsocketperm` is best-effort NTFS |

RDB/AOF files are the same format as official 8.10.0. A 5.0 RDB can be loaded
into 8.10; going back 8.10 → 5.0 is not supported.

## QFork, pagefile, and ASLR

There is no `fork()` on Windows. Background RDB/AOF is supposed to snapshot
the heap by mapping pagefile sections `PAGE_WRITECOPY` and starting a child
(`CreateProcess` + `--QFork`).

**Intended sizing** (when the mapped heap is live):

- The QFork heap **is** the pagefile. Disk reservation is about **1×** the
  heap. Commit during `BGSAVE` can approach **3×**.
- Default reservation is **10× physical RAM**, capped at **1 TB**.
- `redis-server.exe` is linked **`/DYNAMICBASE:NO`** (ASLR off) so the child
  can map the heap at the same virtual address. `redis-cli` and other tools
  keep ASLR. Crash dumps of the server are therefore more predictable; collect
  `.dmp` via Windows Error Reporting if you need them.

**5.0 `--maxheap`:** that flag sized the pagefile mapping. 8.10 does **not**
have a `maxheap` config key yet. Bound the dataset with **`maxmemory`**. Set
`persistence-available no` if you do not want QFork init at all (no
`BGSAVE` / `BGREWRITEAOF` / `SYNC` / `PSYNC` / `REPLCONF` / `BACKUP`).

**Current `win-8.10` gap:** jemalloc still allocates via `VirtualAlloc`
(`g_BypassMemoryMapOnAlloc=1`) because a mapped-heap 16-byte OOM hits during
`aeApiCreate`. Persistence still works: the parent writes the RDB/AOF and
reaps a dummy `--QForkExit` child. Real copy-on-write needs bypass=0. Watch
the Event Log for `MSG_ERROR_1` inserts such as
`QForkParentInit failed: … gle=…` and `CreateFileMapping/pagefile: …`.

Pagefile too small: either grow the Windows pagefile, lower `maxmemory`, or
use `persistence-available no`.

Working-set numbers in Task Manager are **not** the Redis dataset size after
a background save. Use `INFO memory` (`used_memory`).

## maxmemory

Always set `maxmemory` in production. Without it, Redis will grow until the
process or the pagefile fails. `maxmemory-policy` is the usual eviction
knob (`noeviction` by default). Replica output buffers still count against
the limit; leave headroom if you have replicas.

## Persistence extras

- **MP-AOF:** Redis 8 multi-part AOF (`appendonlydir`, manifest). Windows
  must **close** an AOF/manifest fd before `rename`; NTFS cannot rename an
  open file, and Windows `rename` does not overwrite.
- **BACKUP:** `BACKUP START` / `SEAL` uses `link()` → `CreateHardLinkA`,
  with `CopyFile` fallback.
- **Diskless replication:** `repl-diskless-sync` is wired
  (`WSADuplicateSocket`). End-to-end replica PSYNC can still stall after the
  master PING (IOCP handshake); treat diskless replica sync as not yet
  verified.

## Event Log

`syslog-enabled yes` and `syslog-ident` write the Application log. Source
name is `redis` (installed with `--service-install`). Message IDs 0x0–0x3
are frozen (`MSG_INFO_1` … `MSG_SUCCESS_1`). A process started as a service
turns Event Log on even if the conf leaves syslog off.

## Windows Service

See [Windows Service Documentation.md](Windows%20Service%20Documentation.md).
Default account is `NT AUTHORITY\NetworkService`. Install grants that account
ACL on the executable directory, `--dir`, the conf directory, and the
logfile directory. MSI / Chocolatey are **not** in this program (5.0 MSI
notes are historical only).

Prefer a conf file for `--requirepass` / ACL rather than putting secrets on
the SCM ImagePath.

## IO threads

Leave `io-threads` at **1** (upstream default). Values `> 1` are not
recommended until the M9 freeze + delay-associate work is proven. You can
still set it; it is not rejected.

## AF_UNIX

`unixsocket` is compiled and the connection type is registered. Listening
on an NTFS reparse point is **M9** (not ready). TCP is the supported path
today. Minimum OS for AF_UNIX is Windows 10 1803 / Server 2019.

## TLS

Default `win-8.10` is built with `USE_OPENSSL=0`. TLS (`tls-port`, …) is
**M8**. Use TCP + ACL (and a tunnel) until then.

## Modules

`loadmodule path\to\mod.dll` uses `LoadLibrary` / `GetProcAddress` (POSIX
`dlopen`/`dlsym` wrappers). The DLL must **export** `RedisModule_OnLoad`
(C linkage). The in-tree `helloworld.dll` is built that way
(`/EXPORT:RedisModule_OnLoad`).

Windows is LLP64 (`unsigned long` is 32-bit). `redismodule.h`
`RedisModule_Init` casts `GetApi` through `uintptr_t` on `_WIN32`. Rebuild
modules against this header; a module compiled with stock upstream
`unsigned long` casts will crash on load.

Module allocations **must** go through `RedisModule_Alloc` (and friends),
not `malloc`/`HeapAlloc`, so they sit on the Redis heap.

In-tree **vector-sets** (`VADD`, `VSIM`, `VCARD`, …) are compiled into
`redis-server` when CMake `INCLUDE_VEC_SETS=ON` (the default). They are
not a separate `.dll`. HNSW uses the scalar distance path on Windows
(clang-cl has no `__cpu_model` for `__builtin_cpu_supports`).

Bundled Rust modules (Search, JSON, Time Series, Bloom) are out of scope.

`RedisModule_Fork` cannot return 0 into the caller (`CreateProcess` is not
POSIX `fork`). Without a registration it returns **-1** and logs
`RedisModule_Fork is not supported on Windows without RedisModule_SetForkChildFn`.

`RedisModule_SetForkChildFn(ctx, exported_name, user_data)` (Windows only)
registers an **exported symbol name**. The QFork child `LoadLibrary`s the
module and `GetProcAddress`s that name. A raw parent function pointer is
invalid in the child (the `.dll` may load at a different base; ASLR is off
only on `redis-server.exe`). `user_data` is only meaningful if it lives on
the COW heap.

## What this port does not do

- 32-bit
- Cygwin / MSYS2 / MinGW runtime
- `daemonize` as a real fork
- systemd / `setproctitle` / THP / Linux OOM score
- Field-for-field `/proc` `INFO` (Windows has no `/proc`)
- Faithful `madvise` CoW accounting
