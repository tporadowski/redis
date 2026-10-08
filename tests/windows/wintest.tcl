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

# Default units: skip-list + --tags -needs:repl -repl -cluster.
# Add a unit here only after a solo runtest-win.ps1 -Single pass.
# Deferred names: tests/windows/DEFERRED-TESTS.md
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

# SYNC / replica-stream / cluster stay denied until DEFERRED-TESTS.md says otherwise.
# Progress timeout is 600s so 10k-key SORT / list stress can finish on the
# mapped heap. A solo unit/scan still uses 900s; write-load and issue #4906
# stay in the skip-list, so the fenced unit itself finishes in a few seconds.
set has_single 0
set singles {}
for {set i 0} {$i < [llength $rest]} {incr i} {
    if {[lindex $rest $i] eq "--single"} {
        set has_single 1
        incr i
        lappend singles [lindex $rest $i]
    }
}
set timeout_sec 600
if {(!$has_single && [lsearch -exact $units unit/scan] >= 0) ||
    [lsearch -exact $singles unit/scan] >= 0} {
    set timeout_sec 900
}
set argv [list --clients 1 --timeout $timeout_sec --skipfile tests/windows/skip-list.txt \
    --tags "-needs:repl -repl -cluster"]
if {!$has_single} {
    foreach u $units {
        lappend argv --single $u
    }
}
lappend argv {*}$rest

source [file join $root tests test_helper.tcl]
