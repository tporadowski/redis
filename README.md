# Redis for Windows

This is an **unofficial native Windows port**. It is a **modified** version of
Redis Open Source. It is **not affiliated with, endorsed by, or sponsored by
Redis Ltd.** Any use of Redis Ltd trademarks is subject to applicable law
([trademark policy](https://redis.io/legal/trademark-policy/)).

Two Windows lines live in this repository:

| Line | Branch / tag | License | Who it is for |
|------|----------------|---------|----------------|
| **Redis 8.10 for Windows** | `win-8.10` (this tree). First GA tag (when cut): `v8.10.0-win.1` | RSALv2 / SSPLv1 / AGPLv3 (`LICENSE.txt`) | Native 8.10: IOCP, QFork, Service, TLS, cluster, Sentinel |
| **Redis 5.0.14.1 for Windows** | [`win-5.0`](https://github.com/tporadowski/redis/tree/win-5.0) / [`v5.0.14.1`](https://github.com/tporadowski/redis/releases/tag/v5.0.14.1) | BSD-3-Clause | Users who need the older BSD 5.0 port |

`win-4.0.14` is also preserved. Do not merge 8.10 into the BSD branches.

**Notices:** [NOTICE](NOTICE). **Port plan:** [WINDOWS_8.10_PORT_PLAN.md](WINDOWS_8.10_PORT_PLAN.md).

## Redis 8.10 for Windows (`win-8.10`)

Native x64 binaries (no Cygwin). clang-cl + CMake + Ninja.

- **Operators:** [Redis on Windows.md](Redis%20on%20Windows.md) — QFork, pagefile, ASLR, `maxmemory`, AF_UNIX, TLS, IO threads, `RedisModule_Fork`
- **Service:** [Windows Service Documentation.md](Windows%20Service%20Documentation.md)
- **Conf overlays:** [`redis.windows.conf`](redis.windows.conf) (console), [`redis.windows-service.conf`](redis.windows-service.conf) (SCM)
- **Release zip:** `Redis-x64-8.10.0-win.1.zip` (`scripts\pack-win.ps1` / CMake `pack_zip`). MSI / Chocolatey are out of scope
- **OS:** Windows 10 1803 / Server 2019 or later. 32-bit is dropped

### Build and run

```
scripts\build-win.ps1
build\redis-server.exe redis.windows.conf
build\redis-cli.exe PING
```

TLS is on by default (vcpkg OpenSSL 3). `-NoTls` builds a TCP-only server.

### What this port is (and is not)

This tree starts from official Redis Open Source **8.10.0** and adds a Windows
layer (`Win32_Interop`, IOCP, QFork). It is **not** the Redis Ltd product, not
Redis Cloud, and not the official Linux Docker image.

Bundled Rust modules (Search / JSON / Time Series / Bloom) are **out of
scope**. In-tree vector-sets compile into `redis-server` (`INCLUDE_VEC_SETS`).

Known gaps and the next work queue:
[WINDOWS_8.10_REMAINING.md](WINDOWS_8.10_REMAINING.md) (mapped-heap QFork
COW, replica PSYNC, wider tests).

## Redis 5.0.14.1 for Windows (`win-5.0`)

Need **BSD** Redis 5 on Windows? Stay on
[`win-5.0`](https://github.com/tporadowski/redis/tree/win-5.0) / tag
[`v5.0.14.1`](https://github.com/tporadowski/redis/releases/tag/v5.0.14.1).
That line is Visual Studio / MSBuild, still supported, and **not** replaced
by this 8.10 tree. 8.10 → 5.0 data is not a supported downgrade; a 5.0 RDB
can be loaded by 8.10.

Historical MSOpenTech 3.2 notes live on the 5.0/4.0 wiki and trees. They are
not reproduced here.

## What is Redis?

Redis is an in-memory data structure server (cache, session store, queues,
streams, and more). Official documentation is at
[redis.io/docs](https://redis.io/docs/). Command reference:
[redis.io/commands](https://redis.io/commands/).

Redis Open Source 8.0+ is tri-licensed (RSALv2 / SSPLv1 / AGPLv3). Redis Ltd.
also sells Redis Software and Redis Cloud — those are **not** this port.

## Code contributions

PRs to [tporadowski/redis](https://github.com/tporadowski/redis) on `win-8.10`
are contributions to **this unofficial Windows port**, not to Redis Ltd.

[`CONTRIBUTING.md`](CONTRIBUTING.md) in this tree is the **upstream Redis Ltd
Software Grant / CLA**. It applies if you send a patch **to Redis Ltd**
(redis/redis). It is not a CLA for tporadowski/redis Windows work.

Security reports for this port: open an issue on tporadowski/redis. Upstream
security process is in [`SECURITY.md`](SECURITY.md).

## Redis trademarks

The purpose of a trademark is to identify the goods and services of a person
or company without causing confusion. As the registered owner of its name and
logo, Redis Ltd accepts certain limited uses of its trademarks, but it has
requirements that must be followed as described in its
[Trademark Guidelines](https://redis.io/legal/trademark-policy/).

This project does not use Redis Ltd logos and does not claim Redis Ltd
endorsement.
