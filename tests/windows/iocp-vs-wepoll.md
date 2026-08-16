# Decision 15 close-out: stay on IOCP

**PR 9.3.** Date: 2026-08-16. Branch: `win-8.10`.

## Rule (locked)

wepoll is fallback only. `select` is not production. Cut over **only** if a
correctness bug in **IOCP + `SSL_set_fd` + N loops** is unfixed after two
weeks of dedicated work post-9.2.

## Verdict

**Stay on IOCP.** There is no unfixed correctness bug in that combination.
The two-week clock does not start.

Delay-associate remains the first path. Completion forwarding stays the
fallback when a SOCKET is already attached to another port. wepoll is not
vendored and is not a compile-time option.

## What was checked

| Combo | Smoke / evidence | Result |
|-------|------------------|--------|
| One IOCP per `aeEventLoop`, delay-associate | `wsiocp_smoke` (1.2) | ok |
| TLS, single loop: cancel+drain, `SSL_set_fd(SOCKET)` | `smoke_tls.ps1` (8.3) | handshake + SET/GET |
| `io-threads 4` + BGSAVE + AF_UNIX | `smoke_iothreads.ps1` (9.2) | freeze 0 ms; PING/SET/GET/BGSAVE/unix |
| **TLS + `io-threads 4` (the gate)** | `smoke_tls_iothreads.ps1` (9.3) | tls-port PING + 5× SET/GET |

TLS accept still happens on the main loop. `assignClientToIOThread` then
`connTLSRebindEventLoop` → `WSIOCP_SetDestLoop` associates (or marks
forward) **before** the first `SSL_read`. Every `SSL_*` is wrapped with
`CancelIoEx` + drain, then `WSIOCP_RearmRead`.

## Bug list

None of these meet the cut-over rule.

| Item | Status | Why it is not a wepoll trigger |
|------|--------|--------------------------------|
| jemalloc mapped-heap OOM (`g_BypassMemoryMapOnAlloc=1`) | open (3.2/3.3) | Allocator / QFork heap, not IOCP or TLS |
| Replica diskless PSYNC not end-to-end re-verified | open (4.2) | ConnectEx path (7.2); not `SSL_set_fd` + N loops |
| `Sleep(1)` × 1000 BIO freeze (~16 s) | **fixed 9.2** | ThreadControl counted IO threads as BIO workers |
| Notifier/forward pipe never re-armed `WSARecv` | **fixed 9.2** | IOCP readiness; wepoll would hide it, not justify a cut |
| AF_UNIX `AcceptEx` unsupported | **fixed 9.1** | Poll + `accept()`; not TLS |
| Agent Job Object starves Hidden `io-threads` children | env | Same process started outside the job is fine |
| `EAGAIN` vs `EWOULDBLOCK` extra-accept log noise | cosmetic | Empty AcceptEx queue now `EWOULDBLOCK` |

## Why not switch anyway

- Decision 8 already picked delay-associate as the 8.10 model (accept on
  main, associate on the destination loop). That is what 9.2 implements.
- wepoll is epoll-shaped and would drop ConnectEx, per-loop IOCP, and the
  cancel-before-SSL contract for a second Windows backend.
- Forwarding exists for a second rebind. It is not the accept path.

## Re-open

Re-open this note only if a **correctness** bug in IOCP + `SSL_set_fd` +
N loops appears **and** survives two weeks of dedicated work. Performance
complaints, heap/QFork gaps, and diskless PSYNC are not this gate.
