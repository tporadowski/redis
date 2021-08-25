package require Tcl 8.5

set ::tcl_platform(platform) "windows"
source tests/support/server.tcl

set config [dict create "pid" 25044]
set isalive [is_alive $config]
puts "PID=[dict get $config "pid"] alive: $isalive"

while {[is_alive $config]} {
    puts "Process alive, trying to stop it"
    kill_proc $config
    after 2000
}
