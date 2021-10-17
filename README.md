# Redis 5.0.14 for Windows

You can find the release of **Redis 5.0.14 for Windows** on [releases page](https://github.com/tporadowski/redis/releases). Please test it and [report any issues](https://github.com/tporadowski/redis/wiki/Submitting-an-Issue), thanks in advance!

## Redis 4.0.14 for Windows

You can find the release of **Redis 4.0.14 for Windows** on [releases page](https://github.com/tporadowski/redis/releases). Please test it and [report any issues](https://github.com/tporadowski/redis/wiki/Submitting-an-Issue), thanks in advance!

**DISCLAIMER**

At the moment [win-4.0.14 branch](https://github.com/tporadowski/redis/tree/win-4.0.14) provides a **stable port of [Redis 4.0.14](https://github.com/antirez/redis/releases/tag/4.0.14) for Windows x64** and [win-5.0 branch](https://github.com/tporadowski/redis/tree/win-5.0) provides a **stable port of [Redis 5.0.14](https://github.com/redis/redis/releases/tag/5.0.14) for Windows x64**, both merged with archived port of [win-3.2.100 version](https://github.com/MicrosoftArchive/redis/releases/tag/win-3.2.100) from MS Open Tech team. Since the latter is no longer maintained - the sources were merged by hand, projects updated to Visual Studio 2019 (v16.2.5) and any findings (mostly via unit tests) were fixed.

You can find the original description of what this fork provides, how it evolved, what are its requirements, etc. on Wiki: https://github.com/tporadowski/redis/wiki/Old-MSOpenTech-redis-README.md

**Building from source code**

In order to build this project from source code you need to have:
  1. Visual Studio 2019 (i.e. Community Edition, version 16.2.5 or newer) with "C/C++ features" enabled,
  1. Windows SDK 10,
  1. [Git Bash](https://gitforwindows.org/) for Windows or [Cygwin](http://cygwin.com/) with Git - after cloning this repository you need to run `src/mkreleasehdr.sh` script that creates `src/release.h` with some information taken from Git; optionally you can create that file by hand.

**Dependencies**

This project depends on [`jemalloc`](https://github.com/jemalloc/jemalloc) memory allocator, which is slightly customized
in regard to calls to `VirtualAlloc` and `VirtualFree` WinAPI functions. They are being replaced with calls to `AllocHeapBlock/PurgePages`
and `FreeHeapBlock` from `src/Win32_Interop/Win32_QFork.cpp` in order to keep track which memory regions are to be made
available to child processes (for saving RDB/AOF).

Changes to `jemalloc` that provide those customizations are being maintained in [tporadowski/jemalloc repository](https://github.com/tporadowski/jemalloc)
and are copied over to `deps/jemalloc`.
