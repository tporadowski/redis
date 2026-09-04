# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# Portable file helpers + Windows process lookup for the test harness.

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
        return $::env(REDIS_SERVER)
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

proc win32_pid_alive {pid} {
    if {[catch {exec tasklist.exe /FI "PID eq $pid" /NH} out]} {
        return 0
    }
    if {[string match -nocase "*No tasks*" $out]} {
        return 0
    }
    return [expr {[string first $pid $out] != -1}]
}

proc win32_kill_pid {pid} {
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
    lindex [win32_child_pids $parent] 0
}

proc win32_child_pids {parent} {
    if {![string is integer -strict $parent] || $parent <= 0} {
        return {}
    }
    set script "(Get-CimInstance Win32_Process -Filter \"ParentProcessId=$parent\").ProcessId"
    if {[catch {exec powershell.exe -NoProfile -Command $script} out]} {
        return {}
    }
    set pids {}
    foreach line [split $out \n] {
        set line [string trim $line]
        if {[string is integer -strict $line] && $line > 0} {
            lappend pids $line
        }
    }
    return $pids
}
