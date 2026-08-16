# Redis on Windows — release notes

## 8.10 (`win-8.10`)

Unofficial native port of Redis Open Source 8.10.0. Tri-licensed
(RSALv2 / SSPLv1 / AGPLv3). Not affiliated with Redis Ltd.

- x64 only. clang-cl + CMake + Ninja.
- QFork persistence, IOCP, Windows Service, Event Log.
- Conf overlays: `redis.windows.conf`, `redis.windows-service.conf`.
- Operator notes: `Redis on Windows.md`.
- First GA tag (when cut): `v8.10.0-win.1`. Default GitHub branch switches
  to `win-8.10` at that GA.

Known gaps on current `win-8.10` (see the port plan tracker): jemalloc still
uses `VirtualAlloc` (mapped-heap OOM); replica diskless PSYNC is not
end-to-end re-verified. IOCP is the production backend (9.3: stay; no
wepoll cut-over). 10.1 runs a 5.0-first Tcl subset + 5.0 RDB load.

## 5.0 and earlier

BSD line: branch `win-5.0` / tag `v5.0.14.1`. Historical MSOpenTech 3.2
notes lived in older trees; they are not reproduced here.
