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
