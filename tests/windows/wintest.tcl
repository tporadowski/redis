# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# 13.1 Windows entry: widened 8.10 subset under the DPC-watchdog fences.
#
# From the repo root:
#   tclsh tests/windows/wintest.tcl --build build
#   tclsh tests/windows/wintest.tcl --list-units
# Extra test_helper flags after --build are forwarded.
# Prefer tests/windows/runtest-win.ps1 (one unit per process + leftover kill).

package require Tcl 8.5

set build ""
set rest {}
set list_units 0
for {set i 0} {$i < [llength $argv]} {incr i} {
    set o [lindex $argv $i]
    if {$o eq "--build"} {
        incr i
        set build [lindex $argv $i]
    } elseif {$o eq "--list-units"} {
        set list_units 1
    } else {
        lappend rest $o
    }
}

# Every tests/unit, tests/unit/type, and tests/unit/cluster stem, plus the
# Windows and integration names the Windows runner must execute.
# tests/unit/moduleapi is omitted: this CMake build does not produce those
# test modules as DLLs. Skip-list is only facilities Windows does not provide.
set units {
    unit/printver
    unit/type/incr
    unit/type/string
    unit/type/increx
    unit/type/hash
    unit/type/list-2
    unit/type/list-3
    unit/type/list-4
    windows/type_list_nb
    windows/blpop_one
    windows/type_set_nb
    windows/type_zset_nb
    windows/type_stream_nb
    unit/keyspace
    unit/expire
    unit/auth
    unit/protocol
    unit/quit
    unit/shutdown
    unit/limits
    unit/pubsub
    unit/introspection
    unit/bitops
    unit/bitfield
    unit/geo
    unit/hyperloglog
    unit/slowlog
    unit/info-command
    unit/latency-monitor
    unit/introspection-2
    unit/hotkeys
    unit/dump
    unit/replybufsize
    unit/querybuf
    unit/functions
    unit/aofrw
    unit/lazyfree
    unit/pause
    unit/other
    unit/obuf-limits
    unit/pubsubshard
    unit/client-eviction
    unit/acl
    unit/tracking
    unit/wait
    unit/info-keysizes
    unit/info
    unit/networking
    unit/scan
    unit/multi
    unit/sort
    unit/type/list
    unit/type/set
    unit/type/zset
    unit/type/stream
    unit/type/array
    unit/type/hash-field-expire
    unit/type/hash-templates
    unit/type/stream-cgroups
    unit/scripting
    unit/acl-v2
    unit/gcra
    unit/maxmemory
    unit/memefficiency
    unit/oom-score-adj
    unit/tls
    unit/violations
    unit/cluster/announced-endpoints
    unit/cluster/atomic-slot-migration
    unit/cluster/cli
    unit/cluster/cluster-response-tls
    unit/cluster/cluster-topology-change
    unit/cluster/failure-marking
    unit/cluster/hostnames
    unit/cluster/human-announced-nodename
    unit/cluster/internal-secret
    unit/cluster/links
    unit/cluster/misc
    unit/cluster/multi-slot-operations
    unit/cluster/packet-validation
    unit/cluster/scripting
    unit/cluster/sharded-pubsub
    unit/cluster/slot-ownership
    unit/cluster/slot-stats
    unit/cluster/tls-peer-impersonation
    integration/block-repl
    integration/psync2-reg
    integration/replication-2
    integration/convert-zipmap-hash-on-load
    integration/convert-ziplist-hash-on-load
    integration/convert-ziplist-zset-on-load
    integration/logging
    integration/aof-race
    integration/backup
    windows/iocp
    windows/aof
    windows/regression
}

if {$list_units} {
    foreach u $units { puts $u }
    exit 0
}

# wintest.tcl lives in tests/windows; repo root is two levels up.
set here [file dirname [file normalize [info script]]]
set root [file normalize [file join $here .. ..]]
cd $root

if {$build eq ""} {
    if {[file exists [file join $root build redis-server.exe]]} {
        set build [file join $root build]
    } else {
        puts stderr "usage: tclsh tests/windows/wintest.tcl --build <dir>"
        exit 2
    }
}
set build [file normalize $build]
set srv [file join $build redis-server.exe]
if {![file exists $srv]} {
    puts stderr "missing $srv"
    exit 2
}
set ::env(REDIS_SERVER) $srv
set launcher [file join $build redis-test-launcher.exe]
if {[file exists $launcher]} {
    set ::env(REDIS_TEST_LAUNCHER) $launcher
}
if {![info exists ::env(QFORK_HEAP_BYTES)] || $::env(QFORK_HEAP_BYTES) eq ""} {
    set ::env(QFORK_HEAP_BYTES) 512M
}

# No tag denies. needs:repl, repl, and cluster tests run.
# Progress timeout is 1800s: one Tcl body (10k SORT, long KEYS, SCAN #4906,
# script kill) can sit without a harness message for several minutes and
# must not be killed while it is still doing the work the test asked for.
set has_single 0
for {set i 0} {$i < [llength $rest]} {incr i} {
    if {[lindex $rest $i] eq "--single"} {
        set has_single 1
        break
    }
}
set timeout_sec 1800
set argv [list --clients 1 --timeout $timeout_sec --skipfile tests/windows/skip-list.txt]
if {!$has_single} {
    foreach u $units {
        lappend argv --single $u
    }
}
lappend argv {*}$rest

source [file join $root tests test_helper.tcl]
