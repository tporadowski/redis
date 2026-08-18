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

# 13.1 units that stay green with skip-list + --tags -needs:repl -repl -cluster.
# Deferred names: tests/windows/DEFERRED-TESTS.md
set units {
    unit/printver
    unit/type/incr
    unit/type/string
    unit/keyspace
    unit/expire
    unit/auth
    unit/protocol
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
if {![info exists ::env(QFORK_HEAP_BYTES)] || $::env(QFORK_HEAP_BYTES) eq ""} {
    set ::env(QFORK_HEAP_BYTES) 512M
}

# SYNC / replica-stream / cluster stay denied until DEFERRED-TESTS.md says otherwise.
set argv [list --clients 1 --timeout 180 --skipfile tests/windows/skip-list.txt \
    --tags "-needs:repl -repl -cluster"]
set has_single 0
foreach o $rest {
    if {$o eq "--single"} { set has_single 1 }
}
if {!$has_single} {
    foreach u $units {
        lappend argv --single $u
    }
}
lappend argv {*}$rest

source [file join $root tests test_helper.tcl]
