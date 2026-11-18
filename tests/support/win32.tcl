# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# Portable file helpers + exact-image Windows process identity.

if {$::tcl_platform(platform) eq "windows"} {
    set _win_kill_dirs {}
    if {[info exists ::env(REDIS_TEST_LAUNCHER)] && $::env(REDIS_TEST_LAUNCHER) ne ""} {
        lappend _win_kill_dirs [file join [file dirname $::env(REDIS_TEST_LAUNCHER)] test-bin]
    }
    lappend _win_kill_dirs [file join [file dirname [info script]] .. .. build test-bin]
    foreach _win_kill_dir $_win_kill_dirs {
        set _win_kill_dir [file nativename [file normalize $_win_kill_dir]]
        if {[file exists [file join $_win_kill_dir kill.exe]]} {
            set ::env(PATH) "$_win_kill_dir;$::env(PATH)"
            break
        }
    }
    unset _win_kill_dir _win_kill_dirs
}

proc file_contents {filename} {
    set fd [open $filename r]
    set data [read $fd]
    close $fd
    return $data
}

proc file_first_line {filename} {
    set fd [open $filename r]
    set line [gets $fd]
    close $fd
    return $line
}

proc redis_server_bin {} {
    if {[info exists ::env(REDIS_SERVER)] && $::env(REDIS_SERVER) ne ""} {
        return [file normalize $::env(REDIS_SERVER)]
    }
    if {$::tcl_platform(platform) eq "windows"} {
        foreach c {
            build/redis-server.exe
            redis-server.exe
            src/redis-server.exe
        } {
            if {[file exists $c]} { return [file normalize $c] }
        }
        return "redis-server.exe"
    }
    return "src/redis-server"
}

# CMake puts redis-cli next to redis-server. Linux keeps src/redis-cli.
proc redis_cli_bin {} {
    if {$::tcl_platform(platform) eq "windows"} {
        set dir [file dirname [redis_server_bin]]
        foreach name {redis-cli.exe redis-cli} {
            set cand [file join $dir $name]
            if {[file exists $cand]} {
                return [file nativename [file normalize $cand]]
            }
        }
        foreach c {build/redis-cli.exe redis-cli.exe} {
            if {[file exists $c]} {
                return [file nativename [file normalize $c]]
            }
        }
        return "redis-cli.exe"
    }
    return "src/redis-cli"
}

proc redis_test_launcher_bin {} {
    if {[info exists ::env(REDIS_TEST_LAUNCHER)] &&
        $::env(REDIS_TEST_LAUNCHER) ne ""} {
        return [file normalize $::env(REDIS_TEST_LAUNCHER)]
    }
    set dir [file dirname [redis_server_bin]]
    set cand [file join $dir redis-test-launcher.exe]
    if {[file exists $cand]} { return $cand }
    foreach c {
        build/redis-test-launcher.exe
        redis-test-launcher.exe
    } {
        if {[file exists $c]} { return [file normalize $c] }
    }
    return $cand
}

set ::redis_server_path [redis_server_bin]
set ::redis_test_launcher_path [redis_test_launcher_bin]

# Unit tests name modules as tests/modules/<name>.so. On Windows those are
# DLLs produced next to redis-server.exe.
proc redis_test_module {name} {
    if {$::tcl_platform(platform) eq "windows"} {
        set dll [file join [file dirname [redis_server_bin]] "${name}.dll"]
        if {[file exists $dll]} {
            return [file normalize $dll]
        }
    }
    return [file normalize [file join tests modules "${name}.so"]]
}

proc win32_pid_alive {pid} {
    if {![info exists ::redis_test_launcher_path] ||
        ![file exists $::redis_test_launcher_path]} {
        if {[catch {exec tasklist.exe /FI "PID eq $pid" /NH} out]} {
            return 0
        }
        if {[string match -nocase "*No tasks*" $out]} {
            return 0
        }
        return [expr {[string first $pid $out] != -1}]
    }
    return [expr {![catch {
        exec $::redis_test_launcher_path --is-alive $pid
    }]}]
}

# True only when PID is this checkout's redis-server.exe (not a service install).
proc win32_process_matches {pid {expected ""}} {
    if {$expected eq ""} {
        set expected [file nativename [file normalize $::redis_server_path]]
    } else {
        set expected [file nativename [file normalize $expected]]
    }
    if {![file exists $::redis_test_launcher_path]} {
        return [win32_pid_alive $pid]
    }
    return [expr {![catch {
        exec $::redis_test_launcher_path --is-owned $pid $expected
    }]}]
}

proc win32_process_owned {pid} {
    if {![file exists $::redis_test_launcher_path]} {
        return [win32_pid_alive $pid]
    }
    set allowed [list [file nativename [file normalize $::redis_server_path]]]
    foreach extra [list $::redis_test_launcher_path] {
        if {$extra ne "" && [file exists $extra]} {
            lappend allowed [file nativename [file normalize $extra]]
        }
    }
    return [expr {![catch {
        exec $::redis_test_launcher_path --is-owned $pid {*}$allowed
    }]}]
}

proc win32_kill_exe {} {
    if {[info exists ::env(REDIS_TEST_LAUNCHER)] && $::env(REDIS_TEST_LAUNCHER) ne ""} {
        set cand [file join [file dirname $::env(REDIS_TEST_LAUNCHER)] test-bin kill.exe]
        if {[file exists $cand]} { return [file normalize $cand] }
    }
    set cand [file join [file dirname $::redis_server_path] test-bin kill.exe]
    if {[file exists $cand]} { return [file normalize $cand] }
    return ""
}

# Unix `kill` (no signal) is SIGTERM: Redis saves and exits. TerminateProcess
# skips that snapshot, so a later restart has nothing to load.
# force 1 is the escalation path (SIGKILL): the process must die now.
proc win32_kill_pid {pid {force 0}} {
    if {![win32_process_owned $pid]} { return }
    if {!$force} {
        set kill_exe [win32_kill_exe]
        if {$kill_exe ne "" && ![catch {exec $kill_exe -TERM $pid}]} {
            return
        }
    }
    if {[file exists $::redis_test_launcher_path]} {
        set token ""
        catch {
            set token [string trim [exec $::redis_test_launcher_path --creation-token $pid]]
        }
        set expected [file nativename [file normalize $::redis_server_path]]
        if {$token ne ""} {
            if {![catch {
                exec $::redis_test_launcher_path --terminate $pid --token $token $expected
            }]} {
                return
            }
        }
    }
    catch {exec taskkill.exe /F /T /PID $pid}
}

# Start redis-server with args that must fail during startup and return the
# combined diagnostics. Used by Windows branches of official tests that
# cannot fall back on a Unix socket (empty bind, bad config, …).
proc redis_server_startup_error {args} {
    set srv [redis_server_bin]
    set failed [catch {
        exec $srv {*}$args 2>@1
    } output]
    if {!$failed} {
        error "redis-server unexpectedly accepted startup arguments: $args"
    }
    return $output
}

# First child of $parent, or "" if none. Used by get_child_pid (QFork).
proc win32_child_pid {parent} {
    if {[file exists $::redis_test_launcher_path]} {
        set expected [file nativename [file normalize $::redis_server_path]]
        if {![catch {
            set out [string trim [exec $::redis_test_launcher_path \
                --find-qfork-child $parent $expected]]
        }]} {
            if {[string is integer -strict $out] && $out > 0} {
                return $out
            }
        }
        return ""
    }
    lindex [win32_child_pids $parent] 0
}

proc win32_child_pids {parent} {
    if {![string is integer -strict $parent] || $parent <= 0} {
        return {}
    }
    set child [win32_child_pid $parent]
    if {$child eq ""} { return {} }
    return [list $child]
}
