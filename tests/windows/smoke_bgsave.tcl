# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
#
# M3 smoke (3.4): SET/GET + BGSAVE + redis-check-rdb.
# Standalone — does not use tests/test_helper.tcl.
#
#   tclsh tests/windows/smoke_bgsave.tcl <build-dir>
#
# On Windows without tclsh, run tests/windows/smoke_bgsave.ps1 instead.

if {$argc < 1} {
    puts stderr "usage: tclsh smoke_bgsave.tcl <build-dir>"
    exit 2
}

set build [file normalize [lindex $argv 0]]
set exe_ext ""
if {$::tcl_platform(platform) eq "windows"} {
    set exe_ext ".exe"
}

set server [file join $build "redis-server$exe_ext"]
set cli    [file join $build "redis-cli$exe_ext"]
set check  [file join $build "redis-check-rdb$exe_ext"]
foreach {name path} [list redis-server $server redis-cli $cli redis-check-rdb $check] {
    if {![file exists $path]} {
        puts stderr "missing $name: $path"
        exit 2
    }
}

set port 16391
set work [file join $build smoke_bgsave_work]
file mkdir $work
set rdb [file join $work dump.rdb]
set log [file join $work smoke.log]
file delete -force $rdb $log

proc redis {cli port args} {
    set out [exec $cli -p $port {*}$args]
    return [string trim $out]
}

set pid ""
try {
    set pid [exec $server --port $port --bind 127.0.0.1 \
        --protected-mode no --dir $work --dbfilename dump.rdb \
        --enable-debug-command yes --logfile $log &]

    set ready 0
    for {set i 0} {$i < 50} {incr i} {
        if {![catch {redis $cli $port PING} pong] && $pong eq "PONG"} {
            set ready 1
            break
        }
        after 100
    }
    if {!$ready} {
        puts stderr "server did not become ready"
        exit 1
    }

    if {[redis $cli $port SET smoke:key smoke-value] ne "OK"} {
        puts stderr "SET failed"
        exit 1
    }
    if {[redis $cli $port GET smoke:key] ne "smoke-value"} {
        puts stderr "GET mismatch"
        exit 1
    }

    set started [redis $cli $port BGSAVE]
    if {![string match "*Background saving started*" $started]} {
        puts stderr "BGSAVE: $started"
        exit 1
    }

    set ok 0
    for {set i 0} {$i < 100} {incr i} {
        set info [redis $cli $port INFO persistence]
        if {[string match "*rdb_last_bgsave_status:ok*" $info] &&
            [string match "*rdb_bgsave_in_progress:0*" $info]} {
            set ok 1
            break
        }
        if {[string match "*rdb_last_bgsave_status:err*" $info] &&
            [string match "*rdb_bgsave_in_progress:0*" $info]} {
            puts stderr "BGSAVE failed:\n$info"
            exit 1
        }
        after 100
    }
    if {!$ok} {
        puts stderr "BGSAVE did not finish"
        exit 1
    }

    if {![file exists $rdb]} {
        puts stderr "dump.rdb missing"
        exit 1
    }

    set check_out [exec $check $rdb]
    if {![string match "*RDB looks OK*" $check_out]} {
        puts stderr "redis-check-rdb failed:\n$check_out"
        exit 1
    }

    # Snapshot isolation: parent writes during BGSAVE must stay in the
    # parent and must not appear in the RDB the child is writing.
    if {[redis $cli $port SET iso:key before] ne "OK"} {
        puts stderr "iso SET before failed"
        exit 1
    }
    if {[catch {redis $cli $port DEBUG POPULATE 20000} pop] ||
        ![string match "*OK*" $pop]} {
        puts stderr "DEBUG POPULATE failed: $pop"
        exit 1
    }
    set before_info [redis $cli $port INFO persistence]
    set last_save0 0
    regexp {rdb_last_save_time:(\d+)} $before_info -> last_save0
    set started [redis $cli $port BGSAVE]
    if {![string match "*Background saving started*" $started]} {
        puts stderr "iso BGSAVE: $started"
        exit 1
    }
    set saw_progress 0
    for {set i 0} {$i < 200} {incr i} {
        set info [redis $cli $port INFO persistence]
        if {[string match "*rdb_bgsave_in_progress:1*" $info]} {
            if {[redis $cli $port SET iso:key after] ne "OK"} {
                puts stderr "iso SET after failed"
                exit 1
            }
            set saw_progress 1
            break
        }
        after 10
    }
    set ok 0
    for {set i 0} {$i < 200} {incr i} {
        set info [redis $cli $port INFO persistence]
        set last_save1 $last_save0
        regexp {rdb_last_save_time:(\d+)} $info -> last_save1
        if {[string match "*rdb_last_bgsave_status:err*" $info] &&
            [string match "*rdb_bgsave_in_progress:0*" $info] &&
            $last_save1 != $last_save0} {
            puts stderr "iso BGSAVE failed:\n$info"
            exit 1
        }
        if {[string match "*rdb_last_bgsave_status:ok*" $info] &&
            [string match "*rdb_bgsave_in_progress:0*" $info] &&
            $last_save1 != $last_save0} {
            set ok 1
            break
        }
        after 100
    }
    if {!$ok} {
        puts stderr "iso BGSAVE did not finish"
        exit 1
    }
    if {$saw_progress} {
        if {[redis $cli $port GET iso:key] ne "after"} {
            puts stderr "parent lost write issued during BGSAVE"
            exit 1
        }
    }

    catch {redis $cli $port SHUTDOWN NOSAVE}
    set pid ""

    if {$saw_progress} {
        set pid [exec $server --port $port --bind 127.0.0.1 \
            --protected-mode no --dir $work --dbfilename dump.rdb \
            --logfile $log &]
        set ready 0
        for {set i 0} {$i < 50} {incr i} {
            if {![catch {redis $cli $port PING} pong] && $pong eq "PONG"} {
                set ready 1
                break
            }
            after 100
        }
        if {!$ready} {
            puts stderr "reload server did not become ready"
            exit 1
        }
        set got [redis $cli $port GET iso:key]
        if {$got ne "before"} {
            puts stderr "RDB was not isolated: iso:key=$got (want before)"
            exit 1
        }
        catch {redis $cli $port SHUTDOWN NOSAVE}
        set pid ""
        puts "ok smoke_bgsave (SET/GET + BGSAVE + redis-check-rdb + snapshot isolation)"
    } else {
        puts "ok smoke_bgsave (SET/GET + BGSAVE + redis-check-rdb; isolation window missed)"
    }
} on error {msg} {
    puts stderr $msg
    exit 1
} finally {
    if {$pid ne ""} {
        catch {exec $cli -p $port SHUTDOWN NOSAVE}
    }
}

exit 0
