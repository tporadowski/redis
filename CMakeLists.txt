# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# Windows port build (win-8.10). clang-cl + CMake, x64 only.
cmake_minimum_required(VERSION 3.20)
project(redis-windows VERSION 8.10.0 LANGUAGES C CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "This CMake build is the native Windows port (branch win-8.10).")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "win-8.10 is x64 only.")
endif()

option(INCLUDE_VEC_SETS "Compile in-tree vector-sets into redis-server" ON)
option(BUILD_TLS "Link OpenSSL 3 and compile tls.c (USE_OPENSSL=1)" ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W3 /wd4996 /wd4244 /wd4267 /wd4047 /wd4024)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
endif()

# ---------------------------------------------------------------------------
# release.h
# ---------------------------------------------------------------------------
set(RELEASE_H "${CMAKE_BINARY_DIR}/release.h")
find_program(GIT_EXECUTABLE git)
find_program(BASH_EXECUTABLE bash
    PATHS "C:/Program Files/Git/bin" "C:/Program Files (x86)/Git/bin"
    NO_DEFAULT_PATH)

if(GIT_EXECUTABLE AND BASH_EXECUTABLE)
    add_custom_command(
        OUTPUT "${RELEASE_H}"
        COMMAND "${BASH_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/src/mkreleasehdr.sh"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/src/release.h" "${RELEASE_H}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/src"
        DEPENDS "${CMAKE_SOURCE_DIR}/src/mkreleasehdr.sh"
        COMMENT "Generating release.h from git")
else()
    file(WRITE "${RELEASE_H}"
"#define REDIS_GIT_SHA1 \"00000000\"\n#define REDIS_GIT_DIRTY \"0\"\n#define REDIS_BUILD_ID \"windows-cmake\"\n#include \"version.h\"\n#define REDIS_BUILD_ID_RAW REDIS_VERSION REDIS_BUILD_ID REDIS_GIT_DIRTY REDIS_GIT_SHA1\n")
    message(STATUS "Git Bash not found; wrote fallback ${RELEASE_H}")
endif()
add_custom_target(redis_release_h DEPENDS "${RELEASE_H}")

# ---------------------------------------------------------------------------
# Common include helper for Redis sources
# ---------------------------------------------------------------------------
set(REDIS_POSIX_INC "${CMAKE_SOURCE_DIR}/src/Win32_Interop/posix")
set(REDIS_INTEROP_INC "${CMAKE_SOURCE_DIR}/src/Win32_Interop")
set(REDIS_PRE_HEADER "${REDIS_INTEROP_INC}/win32_pre.h")

function(redis_win32_src target)
    target_include_directories(${target} PRIVATE
        "${REDIS_POSIX_INC}"
        "${REDIS_INTEROP_INC}"
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_BINARY_DIR}")
    if(MSVC)
        target_compile_options(${target} PRIVATE "/FI${REDIS_PRE_HEADER}")
    endif()
    target_compile_definitions(${target} PRIVATE REDIS_STATIC=)
endfunction()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
set(LUA_CORE
    deps/lua/src/lapi.c deps/lua/src/lcode.c deps/lua/src/ldebug.c
    deps/lua/src/ldo.c deps/lua/src/ldump.c deps/lua/src/lfunc.c
    deps/lua/src/lgc.c deps/lua/src/llex.c deps/lua/src/lmem.c
    deps/lua/src/lobject.c deps/lua/src/lopcodes.c deps/lua/src/lparser.c
    deps/lua/src/lstate.c deps/lua/src/lstring.c deps/lua/src/ltable.c
    deps/lua/src/ltm.c deps/lua/src/lundump.c deps/lua/src/lvm.c
    deps/lua/src/lzio.c deps/lua/src/strbuf.c deps/lua/src/fpconv.c)
set(LUA_LIB
    deps/lua/src/lauxlib.c deps/lua/src/lbaselib.c deps/lua/src/ldblib.c
    deps/lua/src/liolib.c deps/lua/src/lmathlib.c deps/lua/src/loslib.c
    deps/lua/src/ltablib.c deps/lua/src/lstrlib.c deps/lua/src/loadlib.c
    deps/lua/src/linit.c deps/lua/src/lua_cjson.c deps/lua/src/lua_struct.c
    deps/lua/src/lua_cmsgpack.c deps/lua/src/lua_bit.c)
add_library(lua STATIC ${LUA_CORE} ${LUA_LIB})
target_include_directories(lua PRIVATE deps/lua/src)
target_compile_definitions(lua PRIVATE LUA_USE_APICHECK= strncasecmp=_strnicmp strcasecmp=_stricmp)
target_compile_options(lua PRIVATE /FIstdint.h)

add_library(hdr_histogram STATIC deps/hdr_histogram/hdr_histogram.c)
target_include_directories(hdr_histogram PUBLIC deps/hdr_histogram "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(hdr_histogram PRIVATE HDR_MALLOC_INCLUDE="hdr_redis_malloc.h")
target_compile_options(hdr_histogram PRIVATE "/clang:-Wno-incompatible-pointer-types")

add_library(fpconv STATIC deps/fpconv/fpconv_dtoa.c)
target_include_directories(fpconv PUBLIC deps/fpconv)

add_library(xxhash STATIC deps/xxhash/xxhash.c)
target_include_directories(xxhash PUBLIC deps/xxhash)

set(TRE_SRC
    deps/tre/lib/regcomp.c deps/tre/lib/regerror.c deps/tre/lib/regexec.c
    deps/tre/lib/tre-ast.c deps/tre/lib/tre-compile.c deps/tre/lib/tre-filter.c
    deps/tre/lib/tre-match-backtrack.c deps/tre/lib/tre-match-parallel.c
    deps/tre/lib/tre-mem.c deps/tre/lib/tre-parse.c deps/tre/lib/tre-stack.c
    deps/tre/lib/xmalloc.c)
add_library(tre STATIC ${TRE_SRC})
target_include_directories(tre PUBLIC deps/tre/local_includes deps/tre/lib)
target_compile_definitions(tre PRIVATE TRE_REGEX_T_FIELD=value)
target_compile_options(tre PRIVATE "/FI${REDIS_INTEROP_INC}/win32_types_hiredis.h")

add_library(linenoise STATIC src/Win32_Interop/linenoise_win32.c)
target_include_directories(linenoise PUBLIC deps/linenoise)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
if(BUILD_TLS)
    # hiredis_ssl + tls.c. OpenSSL 3.x via vcpkg x64-windows (MSVC ABI).
    find_package(OpenSSL REQUIRED)
    if(OPENSSL_VERSION VERSION_LESS "3.0.0")
        message(WARNING "win-8.10 wants OpenSSL 3.x; found ${OPENSSL_VERSION}")
    endif()
    set(ENABLE_SSL ON CACHE BOOL "Build hiredis_ssl" FORCE)
    message(STATUS "BUILD_TLS=ON OpenSSL ${OPENSSL_VERSION} (${OPENSSL_INCLUDE_DIR})")
endif()
add_subdirectory(deps/hiredis EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# jemalloc 5.3 (page hooks; retain OFF; LG_PAGE=20). 4 MB QFork maps
# are 4× 1 MB slots so jemalloc PAGE matches the QFork slot. No jemalloc_cpp.cpp.
# ---------------------------------------------------------------------------
set(JEMALLOC_SRC
    deps/jemalloc/src/jemalloc.c
    deps/jemalloc/src/arena.c
    deps/jemalloc/src/background_thread.c
    deps/jemalloc/src/base.c
    deps/jemalloc/src/bin.c
    deps/jemalloc/src/bin_info.c
    deps/jemalloc/src/bitmap.c
    deps/jemalloc/src/buf_writer.c
    deps/jemalloc/src/cache_bin.c
    deps/jemalloc/src/ckh.c
    deps/jemalloc/src/counter.c
    deps/jemalloc/src/ctl.c
    deps/jemalloc/src/decay.c
    deps/jemalloc/src/div.c
    deps/jemalloc/src/ecache.c
    deps/jemalloc/src/edata.c
    deps/jemalloc/src/edata_cache.c
    deps/jemalloc/src/ehooks.c
    deps/jemalloc/src/emap.c
    deps/jemalloc/src/eset.c
    deps/jemalloc/src/exp_grow.c
    deps/jemalloc/src/extent.c
    deps/jemalloc/src/extent_dss.c
    deps/jemalloc/src/extent_mmap.c
    deps/jemalloc/src/fxp.c
    deps/jemalloc/src/san.c
    deps/jemalloc/src/san_bump.c
    deps/jemalloc/src/hook.c
    deps/jemalloc/src/hpa.c
    deps/jemalloc/src/hpa_hooks.c
    deps/jemalloc/src/hpdata.c
    deps/jemalloc/src/inspect.c
    deps/jemalloc/src/large.c
    deps/jemalloc/src/log.c
    deps/jemalloc/src/malloc_io.c
    deps/jemalloc/src/mutex.c
    deps/jemalloc/src/nstime.c
    deps/jemalloc/src/pa.c
    deps/jemalloc/src/pa_extra.c
    deps/jemalloc/src/pai.c
    deps/jemalloc/src/pac.c
    deps/jemalloc/src/pages.c
    deps/jemalloc/src/peak_event.c
    deps/jemalloc/src/prof.c
    deps/jemalloc/src/prof_data.c
    deps/jemalloc/src/prof_log.c
    deps/jemalloc/src/prof_recent.c
    deps/jemalloc/src/prof_stats.c
    deps/jemalloc/src/prof_sys.c
    deps/jemalloc/src/psset.c
    deps/jemalloc/src/rtree.c
    deps/jemalloc/src/safety_check.c
    deps/jemalloc/src/sc.c
    deps/jemalloc/src/sec.c
    deps/jemalloc/src/stats.c
    deps/jemalloc/src/sz.c
    deps/jemalloc/src/tcache.c
    deps/jemalloc/src/test_hooks.c
    deps/jemalloc/src/thread_event.c
    deps/jemalloc/src/ticker.c
    deps/jemalloc/src/tsd.c
    deps/jemalloc/src/witness.c)
add_library(jemalloc STATIC ${JEMALLOC_SRC})
target_include_directories(jemalloc PUBLIC
    deps/jemalloc/include
    deps/jemalloc/include/msvc_compat)
# Do not apply Redis /FI win32_pre.h — jemalloc has its own Windows compat.
target_compile_definitions(jemalloc PRIVATE
    JEMALLOC_NO_PRIVATE_NAMESPACE
    JEMALLOC_EXPORT=
    _REENTRANT
    USE_WIN32_EXTERNAL_HEAP_ALLOC)
if(MSVC)
    target_compile_options(jemalloc PRIVATE
        /wd4146 /wd4334 /wd4090
        "/clang:-Wno-incompatible-pointer-types"
        "/clang:-Wno-unused-function")
endif()

# ---------------------------------------------------------------------------
# win32_interop
# ---------------------------------------------------------------------------
add_library(win32_interop STATIC
    src/Win32_Interop/Win32_Time.c
    src/Win32_Interop/Win32_Error.c
    src/Win32_Interop/Win32_PThread.c
    src/Win32_Interop/Win32_APIs.c
    src/Win32_Interop/Win32_ANSI.c
    src/Win32_Interop/Win32_cli.c
    src/Win32_Interop/win32_rfdmap.cpp
    src/Win32_Interop/Win32_fdapi_crt.cpp
    src/Win32_Interop/Win32_FDAPI.cpp
    src/Win32_Interop/win32_wsiocp.c
    src/Win32_Interop/Win32_ThreadControl.c
    src/Win32_Interop/Win32_ProcessTable.c
    src/Win32_Interop/Win32_EventLog.cpp
    src/Win32_Interop/Win32_service.cpp)
target_include_directories(win32_interop PUBLIC
    "${REDIS_POSIX_INC}"
    "${REDIS_INTEROP_INC}")
target_link_libraries(win32_interop PUBLIC ws2_32 mswsock advapi32 shell32)
if(MSVC)
    target_compile_options(win32_interop PRIVATE
        $<$<COMPILE_LANGUAGE:C>:/FI${REDIS_PRE_HEADER}>
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
endif()
set_source_files_properties(
    src/Win32_Interop/Win32_Time.c
    src/Win32_Interop/Win32_Error.c
    src/Win32_Interop/Win32_PThread.c
    src/Win32_Interop/Win32_APIs.c
    src/Win32_Interop/Win32_ANSI.c
    src/Win32_Interop/Win32_cli.c
    src/Win32_Interop/win32_wsiocp.c
    src/Win32_Interop/Win32_ThreadControl.c
    src/Win32_Interop/Win32_ProcessTable.c
    PROPERTIES LANGUAGE C)
set_source_files_properties(src/Win32_Interop/Win32_ANSI.c
    PROPERTIES COMPILE_DEFINITIONS "UNICODE;_UNICODE")

# ---------------------------------------------------------------------------
# redis-cli
# ---------------------------------------------------------------------------
set(REDIS_CLI_SRC
    src/anet.c src/adlist.c src/dict.c src/redis-cli.c src/zmalloc.c
    src/release.c src/ae.c src/redisassert.c src/crcspeed.c src/crccombine.c
    src/crc64.c src/siphash.c src/crc16.c src/monotonic.c src/cli_common.c
    src/mt19937-64.c src/strl.c src/cli_commands.c)
add_executable(redis-cli ${REDIS_CLI_SRC})
add_dependencies(redis-cli redis_release_h)
target_include_directories(redis-cli PRIVATE
    deps/hiredis deps/linenoise deps/hdr_histogram deps/lua/src
    deps/xxhash deps/fpconv deps/tre/local_includes)
redis_win32_src(redis-cli)
if(BUILD_TLS)
    target_compile_definitions(redis-cli PRIVATE USE_OPENSSL=1)
    target_link_libraries(redis-cli PRIVATE hiredis_ssl OpenSSL::SSL OpenSSL::Crypto)
endif()
target_link_libraries(redis-cli PRIVATE
    hiredis linenoise hdr_histogram win32_interop)

# ---------------------------------------------------------------------------
# redis-benchmark (same objects as upstream Makefile)
# ---------------------------------------------------------------------------
set(REDIS_BENCHMARK_SRC
    src/ae.c src/anet.c src/redis-benchmark.c src/adlist.c src/dict.c
    src/zmalloc.c src/redisassert.c src/release.c src/crcspeed.c
    src/crccombine.c src/crc64.c src/siphash.c src/crc16.c src/monotonic.c
    src/cli_common.c src/mt19937-64.c src/strl.c)
add_executable(redis-benchmark ${REDIS_BENCHMARK_SRC})
add_dependencies(redis-benchmark redis_release_h)
target_include_directories(redis-benchmark PRIVATE
    deps/hiredis deps/hdr_histogram deps/xxhash deps/lua/src)
redis_win32_src(redis-benchmark)
if(BUILD_TLS)
    target_compile_definitions(redis-benchmark PRIVATE USE_OPENSSL=1)
    target_link_libraries(redis-benchmark PRIVATE hiredis_ssl OpenSSL::SSL OpenSSL::Crypto)
endif()
target_link_libraries(redis-benchmark PRIVATE hiredis hdr_histogram win32_interop)

add_executable(fdapi_smoke tests/windows/fdapi_smoke.c)
redis_win32_src(fdapi_smoke)
target_link_libraries(fdapi_smoke PRIVATE win32_interop)

add_executable(wsiocp_smoke
    tests/windows/wsiocp_smoke.c
    src/ae.c src/zmalloc.c src/monotonic.c src/redisassert.c)
redis_win32_src(wsiocp_smoke)
target_link_libraries(wsiocp_smoke PRIVATE win32_interop)

add_executable(pthread_smoke tests/windows/pthread_smoke.c)
redis_win32_src(pthread_smoke)
target_link_libraries(pthread_smoke PRIVATE win32_interop)

add_executable(jemalloc_smoke
    tests/windows/jemalloc_smoke.c
    src/Win32_Interop/Win32_JemallocHeap.c)
target_include_directories(jemalloc_smoke PRIVATE
    deps/jemalloc/include
    "${REDIS_INTEROP_INC}")
target_compile_definitions(jemalloc_smoke PRIVATE JEMALLOC_EXPORT= QFORK_MAIN_IMPL)
set_source_files_properties(src/Win32_Interop/Win32_JemallocHeap.c PROPERTIES LANGUAGE C)
target_link_libraries(jemalloc_smoke PRIVATE jemalloc win32_interop)

add_executable(qfork_heap_smoke
    tests/windows/qfork_heap_smoke.c
    src/Win32_Interop/Win32_QFork_heap.cpp)
target_include_directories(qfork_heap_smoke PRIVATE "${REDIS_INTEROP_INC}")
target_compile_definitions(qfork_heap_smoke PRIVATE QFORK_MAIN_IMPL)
if(MSVC)
    target_compile_options(qfork_heap_smoke PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
endif()
target_link_libraries(qfork_heap_smoke PRIVATE win32_interop)

add_executable(mapped_jemalloc_smoke
    tests/windows/mapped_jemalloc.c
    src/Win32_Interop/Win32_QFork_heap.cpp)
target_include_directories(mapped_jemalloc_smoke PRIVATE
    deps/jemalloc/include
    "${REDIS_INTEROP_INC}")
target_compile_definitions(mapped_jemalloc_smoke PRIVATE
    JEMALLOC_EXPORT= QFORK_MAIN_IMPL QFORK_HEAP_LOG)
if(MSVC)
    target_compile_options(mapped_jemalloc_smoke PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
endif()
target_link_libraries(mapped_jemalloc_smoke PRIVATE jemalloc win32_interop)

# ---------------------------------------------------------------------------
# redis-server (M3: jemalloc + QFork heap + parent-only redisFork)
# ---------------------------------------------------------------------------
set(REDIS_SERVER_SRC
    src/threads_mngr.c src/memory_prefetch.c src/adlist.c src/quicklist.c
    src/ae.c src/anet.c src/dict.c src/ebuckets.c src/eventnotifier.c
    src/iothread.c src/mstr.c src/entry.c src/kvstore.c src/fwtree.c
    src/estore.c src/server.c src/sds.c src/zmalloc.c src/lzf_c.c src/lzf_d.c
    src/pqsort.c src/zipmap.c src/sha1.c src/ziplist.c src/release.c
    src/networking.c src/util.c src/object.c src/db.c src/replication.c
    src/rdb.c src/t_string.c src/t_list.c src/t_set.c src/t_zset.c
    src/t_hash.c src/t_array.c src/sparsearray.c src/config.c src/aof.c
    src/pubsub.c src/multi.c src/debug.c src/sort.c src/intset.c src/syncio.c
    src/cluster.c src/cluster_asm.c src/cluster_legacy.c src/cluster_slot_stats.c
    src/crc16.c src/endianconv.c src/slowlog.c src/eval.c src/bio.c src/rio.c
    src/rand.c src/memtest.c src/syscheck.c src/crcspeed.c src/crccombine.c
    src/crc64.c src/bitops.c src/sentinel.c src/notify.c src/blocked.c
    src/hyperloglog.c src/latency.c src/sparkline.c src/redis-check-rdb.c
    src/redis-check-aof.c src/geo.c src/lazyfree.c src/module.c src/evict.c
    src/expire.c src/geohash.c src/geohash_helper.c src/childinfo.c
    src/defrag.c src/siphash.c src/rax.c src/t_stream.c src/listpack.c
    src/localtime.c src/lolwut.c src/lolwut5.c src/lolwut6.c src/lolwut8.c
    src/acl.c src/tracking.c src/socket.c src/tls.c src/sha256.c src/timeout.c
    src/setcpuaffinity.c src/monotonic.c src/mt19937-64.c src/resp_parser.c
    src/call_reply.c src/script_lua.c src/script.c src/functions.c
    src/function_lua.c src/commands.c src/strl.c src/connection.c src/unix.c
    src/logreqres.c src/keymeta.c src/chk.c src/hotkeys.c src/gcra.c
    src/vector.c src/fast_float_strtod.c src/client_comp.c
    src/Win32_Interop/Win32_QFork.cpp
    src/Win32_Interop/Win32_QFork_heap.cpp
    src/Win32_Interop/Win32_QFork_impl.c
    src/Win32_Interop/resources/EventLog.rc)
if(INCLUDE_VEC_SETS)
    list(APPEND REDIS_SERVER_SRC
        modules/vector-sets/hnsw.c
        modules/vector-sets/vset.c
        modules/vector-sets/vset_config.c)
endif()
set_source_files_properties(src/Win32_Interop/Win32_QFork_impl.c PROPERTIES LANGUAGE C)
if(INCLUDE_VEC_SETS)
    set_source_files_properties(
        modules/vector-sets/hnsw.c
        modules/vector-sets/vset.c
        modules/vector-sets/vset_config.c
        PROPERTIES LANGUAGE C)
endif()
set_source_files_properties(src/Win32_Interop/resources/EventLog.rc PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/src/Win32_Interop/resources")

add_executable(redis-server ${REDIS_SERVER_SRC})
add_dependencies(redis-server redis_release_h)
target_include_directories(redis-server PRIVATE
    deps/hiredis deps/lua/src deps/hdr_histogram
    deps/xxhash deps/fpconv deps/tre/local_includes
    deps/jemalloc/include)
redis_win32_src(redis-server)
if(BUILD_TLS)
    target_compile_definitions(redis-server PRIVATE
        USE_OPENSSL=1 BUILD_TLS_MODULE=0 USE_LOCAL_TRE_H
        USE_JEMALLOC JEMALLOC_EXPORT=)
    target_link_libraries(redis-server PRIVATE hiredis_ssl OpenSSL::SSL OpenSSL::Crypto)
else()
    target_compile_definitions(redis-server PRIVATE
        USE_OPENSSL=0 BUILD_TLS_MODULE=0 USE_LOCAL_TRE_H
        USE_JEMALLOC JEMALLOC_EXPORT=)
endif()
if(INCLUDE_VEC_SETS)
    target_compile_definitions(redis-server PRIVATE INCLUDE_VEC_SETS=1)
    target_include_directories(redis-server PRIVATE
        "${CMAKE_SOURCE_DIR}/modules/vector-sets")
endif()
target_link_libraries(redis-server PRIVATE
    lua hdr_histogram hiredis fpconv xxhash tre win32_interop jemalloc
    advapi32 shell32)
if(MSVC)
    target_compile_options(redis-server PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
    # Child must map the QFork heap at the same VA.
    target_link_options(redis-server PRIVATE /DYNAMICBASE:NO)
endif()

# redis-check-rdb is the same image; dispatch is by argv0.
add_custom_command(TARGET redis-server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:redis-server>
        $<TARGET_FILE_DIR:redis-server>/redis-check-rdb.exe
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:redis-server>
        $<TARGET_FILE_DIR:redis-server>/redis-check-aof.exe
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:redis-server>
        $<TARGET_FILE_DIR:redis-server>/redis-sentinel.exe
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/redis.windows.conf"
        "${CMAKE_SOURCE_DIR}/redis.windows-service.conf"
        $<TARGET_FILE_DIR:redis-server>
    COMMENT "Copy redis-check-rdb.exe, redis-sentinel.exe, Windows conf")
# vcpkg applocal copies libssl/libcrypto next to the exes.

add_custom_target(smoke_bgsave
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/tests/windows/smoke_bgsave.ps1"
        -BuildDir "${CMAKE_BINARY_DIR}"
    DEPENDS redis-server redis-cli
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "M3 smoke: SET/GET + BGSAVE + redis-check-rdb")

add_custom_target(smoke_service
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/tests/windows/smoke_service.ps1"
        -BuildDir "${CMAKE_BINARY_DIR}"
    DEPENDS redis-server redis-cli
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "M5 smoke: console PING + --service-run (no SCM)")

# ---------------------------------------------------------------------------
# hello-world module (6.1)
# ---------------------------------------------------------------------------
add_library(helloworld SHARED src/modules/helloworld.c)
set_source_files_properties(src/modules/helloworld.c PROPERTIES LANGUAGE C)
target_include_directories(helloworld PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${REDIS_POSIX_INC}"
    "${REDIS_INTEROP_INC}")
set_target_properties(helloworld PROPERTIES
    PREFIX ""
    OUTPUT_NAME helloworld
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if(MSVC)
    target_compile_options(helloworld PRIVATE "/FI${REDIS_PRE_HEADER}")
    target_link_options(helloworld PRIVATE /EXPORT:RedisModule_OnLoad)
endif()

add_library(forkcheck SHARED src/modules/forkcheck.c)
set_source_files_properties(src/modules/forkcheck.c PROPERTIES LANGUAGE C)
target_include_directories(forkcheck PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${REDIS_POSIX_INC}"
    "${REDIS_INTEROP_INC}")
set_target_properties(forkcheck PROPERTIES
    PREFIX ""
    OUTPUT_NAME forkcheck
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if(MSVC)
    target_compile_options(forkcheck PRIVATE "/FI${REDIS_PRE_HEADER}")
    target_link_options(forkcheck PRIVATE /EXPORT:RedisModule_OnLoad)
endif()

add_custom_target(smoke_module
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/tests/windows/smoke_module.ps1"
        -BuildDir "${CMAKE_BINARY_DIR}"
    DEPENDS redis-server redis-cli helloworld
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "M6 smoke: loadmodule helloworld.dll + HELLO.SIMPLE")

add_custom_target(smoke_vecset
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/tests/windows/smoke_vecset.ps1"
        -BuildDir "${CMAKE_BINARY_DIR}"
    DEPENDS redis-server redis-cli
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "M6 smoke: in-tree VADD/VCARD/VSIM")

add_custom_target(smoke_fork
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/tests/windows/smoke_fork.ps1"
        -BuildDir "${CMAKE_BINARY_DIR}"
    DEPENDS redis-server redis-cli forkcheck
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "M6 smoke: RedisModule_Fork without SetForkChildFn is -1")

add_custom_target(smoke_sentinel
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/tests/windows/smoke_sentinel.ps1"
        -BuildDir "${CMAKE_BINARY_DIR}"
    DEPENDS redis-server redis-cli
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "M7 smoke: Sentinel + notification-script reap")

add_custom_target(smoke_tls
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/tests/windows/smoke_tls.ps1"
        -BuildDir "${CMAKE_BINARY_DIR}"
    DEPENDS redis-server redis-cli
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "M8 smoke: tls-port handshake + GET/SET")

add_custom_target(smoke_unix
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/tests/windows/smoke_unix.ps1"
        -BuildDir "${CMAKE_BINARY_DIR}"
    DEPENDS redis-server redis-cli
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "M9 smoke: AF_UNIX listen + PING")

add_custom_target(smoke_iothreads
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/tests/windows/smoke_iothreads.ps1"
        -BuildDir "${CMAKE_BINARY_DIR}"
    DEPENDS redis-server redis-cli
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "M9 smoke: io-threads 4 + BGSAVE + unix PING")

add_custom_target(smoke_tls_iothreads
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/tests/windows/smoke_tls_iothreads.ps1"
        -BuildDir "${CMAKE_BINARY_DIR}"
    DEPENDS redis-server redis-cli
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "M9 gate: tls-port + io-threads 4")

add_custom_target(wintest
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/tests/windows/runtest-win.ps1"
        -BuildDir "${CMAKE_BINARY_DIR}"
    DEPENDS redis-server redis-cli
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "13.x: 5.0 RDB load + fenced Tcl units (one process each)")

# ---------------------------------------------------------------------------
# 10.2 zip: Redis-x64-8.10.0-win.1
# ---------------------------------------------------------------------------
set(REDIS_WIN_VERSION "${PROJECT_VERSION}")
set(REDIS_WIN_PORT_REV "1")
set(REDIS_WIN_ZIP_STEM "Redis-x64-${REDIS_WIN_VERSION}-win.${REDIS_WIN_PORT_REV}")

install(TARGETS redis-server redis-cli redis-benchmark
    RUNTIME DESTINATION .)
install(PROGRAMS
    "${CMAKE_BINARY_DIR}/redis-check-rdb.exe"
    "${CMAKE_BINARY_DIR}/redis-check-aof.exe"
    "${CMAKE_BINARY_DIR}/redis-sentinel.exe"
    DESTINATION .)
install(FILES
    "${CMAKE_SOURCE_DIR}/redis.windows.conf"
    "${CMAKE_SOURCE_DIR}/redis.windows-service.conf"
    "${CMAKE_SOURCE_DIR}/LICENSE.txt"
    "${CMAKE_SOURCE_DIR}/REDISCONTRIBUTIONS.txt"
    "${CMAKE_SOURCE_DIR}/NOTICE"
    "${CMAKE_SOURCE_DIR}/Redis on Windows.md"
    "${CMAKE_SOURCE_DIR}/Redis on Windows Release Notes.md"
    "${CMAKE_SOURCE_DIR}/Windows Service Documentation.md"
    DESTINATION .)
if(BUILD_TLS)
    install(FILES
        "${CMAKE_BINARY_DIR}/libssl-3-x64.dll"
        "${CMAKE_BINARY_DIR}/libcrypto-3-x64.dll"
        DESTINATION .)
endif()

add_custom_target(pack_zip
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/scripts/pack-win.ps1"
        -BuildDir "${CMAKE_BINARY_DIR}"
        -SourceDir "${CMAKE_SOURCE_DIR}"
        -ZipStem "${REDIS_WIN_ZIP_STEM}"
    DEPENDS redis-server redis-cli redis-benchmark
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "M10: zip ${REDIS_WIN_ZIP_STEM}.zip")

add_custom_target(smoke_zip
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/tests/windows/smoke_zip.ps1"
        -BuildDir "${CMAKE_BINARY_DIR}"
        -ZipStem "${REDIS_WIN_ZIP_STEM}"
    DEPENDS pack_zip
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "M10: verify zip layout + redis-cli --version")

add_custom_target(lint_llp64
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_SOURCE_DIR}/tests/windows/lint-llp64.ps1"
        -SourceDir "${CMAKE_SOURCE_DIR}"
        -BuildDir "${CMAKE_BINARY_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "M10: LLP64 sizeof(long)/clzl/PORT_LONG lint + arch_bits:64")

message(STATUS "win-8.10 CMake: redis-cli + redis-server + helloworld.dll + smokes")
