# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# Windows port build (win-8.10). clang-cl + CMake, x64 only.
cmake_minimum_required(VERSION 3.20)
project(redis-windows VERSION 8.10.0 LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This CMake build is the native Windows port (branch win-8.10).")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "win-8.10 is x64 only.")
endif()

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
add_subdirectory(deps/hiredis EXCLUDE_FROM_ALL)

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
    src/Win32_Interop/win32_wsiocp.c)
target_include_directories(win32_interop PUBLIC
    "${REDIS_POSIX_INC}"
    "${REDIS_INTEROP_INC}")
target_link_libraries(win32_interop PUBLIC ws2_32 mswsock)
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
target_link_libraries(redis-cli PRIVATE
    hiredis linenoise hdr_histogram win32_interop)

add_executable(fdapi_smoke tests/windows/fdapi_smoke.c)
redis_win32_src(fdapi_smoke)
target_link_libraries(fdapi_smoke PRIVATE win32_interop)

add_executable(wsiocp_smoke
    tests/windows/wsiocp_smoke.c
    src/ae.c src/zmalloc.c src/monotonic.c src/redisassert.c)
redis_win32_src(wsiocp_smoke)
target_link_libraries(wsiocp_smoke PRIVATE win32_interop)

message(STATUS "win-8.10 CMake: redis-cli + fdapi_smoke + deps")
