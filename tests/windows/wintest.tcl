# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# 10.1 Windows entry: 5.0-first subset of the 8.10 suite.
#
# From the repo root:
#   tclsh tests/windows/wintest.tcl --build build
# Extra test_helper flags after --build are forwarded.

package require Tcl 8.5

set build ""
set rest {}
for {set i 0} {$i < [llength $argv]} {incr i} {
    set o [lindex $argv $i]
    if {$o eq "--build"} {
        incr i
        set build [lindex $argv $i]
    } else {
        lappend rest $o
    }
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

# 5.0-first units that stay green on win-8.10. type/string is next
# (MSET prefetch / 10k SET are slow or I/O-error on this port).
set units {
    unit/printver
    unit/type/incr
}

set argv [list --clients 1 --timeout 600 --skipfile tests/windows/skip-list.txt]
foreach u $units {
    lappend argv --single $u
}
lappend argv {*}$rest

source [file join $root tests test_helper.tcl]
