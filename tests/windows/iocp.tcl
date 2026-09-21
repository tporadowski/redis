# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# IOCP one-shot contract: busy-script batching, listener close with a pending
# AcceptEx, refused ConnectEx, and bounded write rearm. Adapted from pwin32
# tests/windows/iocp.tcl. Does not source the official extra-client storms.

proc windows_iocp_capture_reply {client type reply} {
    set ::windows_iocp_probe_reply [list $type $reply]
}

proc windows_iocp_client_has_write_event {name} {
    set ::windows_iocp_client_line [lsearch -inline \
        [split [r client list] "\r\n"] "*name=$name*"]
    return [regexp {events=[^ ]*w} $::windows_iocp_client_line]
}

start_server {tags {"windows iocp regression network scripting external:skip tls:skip needs:debug"} overrides {lua-time-limit 10}} {
    test {Windows IOCP preserves batched reads across nested busy-script event loops} {
        set sleeper [redis_deferring_client]
        set runner [redis_deferring_client]
        set probe [redis_client]
        set probe_id [$probe client id]
        $probe blocking 0
        set ::windows_iocp_probe_reply pending

        # Hold the Redis thread while the kernel queues two completions in
        # a stable order. The first starts an infinite script; the second
        # must remain visible to the nested busy-script event loop.
        $sleeper debug sleep 2
        after 200
        $runner eval {while true do end} 0
        after 200
        $probe ping windows_iocp_capture_reply
        after 2000

        # Fresh connections continue to work in the broken case, so use
        # them to separate IOCP batch loss from ordinary SCRIPT KILL
        # behavior and to guarantee bounded cleanup of the script.
        set busy_client [redis [srv host] [srv port] 0 $::tls]
        set busy_error [catch {$busy_client ping} busy_reply]

        set kill_client [redis [srv host] [srv port] 0 $::tls]
        set kill_error [catch {$kill_client script kill} kill_reply]

        after 200
        set post_client [redis [srv host] [srv port] 0 $::tls]
        set post_error [catch {$post_client ping} post_reply]
        set probe_info [catch {
            $post_client client list id $probe_id
        } probe_client_info]

        set runner_error 0
        set runner_reply not-read
        if {!$kill_error && $kill_reply eq "OK"} {
            set runner_error [catch {$runner read} runner_reply]
        }

        set timeout [after 3000 {
            if {$::windows_iocp_probe_reply eq "pending"} {
                set ::windows_iocp_probe_reply timeout
            }
        }]
        if {$::windows_iocp_probe_reply eq "pending"} {
            vwait ::windows_iocp_probe_reply
        }
        after cancel $timeout
        set probe_reply $::windows_iocp_probe_reply

        catch {$sleeper close}
        catch {$runner close}
        catch {$probe close}
        catch {$busy_client close}
        catch {$kill_client close}
        catch {$post_client close}

        assert_equal 1 $busy_error
        assert_match {BUSY*} $busy_reply
        assert_equal 0 $kill_error
        assert_equal OK $kill_reply
        assert_equal 1 $runner_error
        assert_match {*killed by user*} $runner_reply
        assert_equal 0 $post_error
        assert_equal PONG $post_reply
        if {$probe_reply eq "timeout"} {
            if {$probe_info} {
                set probe_client_info "CLIENT LIST failed: $probe_client_info"
            }
            fail "pre-existing PING timed out; client state: $probe_client_info"
        }
        assert_equal err [lindex $probe_reply 0]
        assert_match {BUSY*} [lindex $probe_reply 1]
    }

    test {Windows IOCP closes a listener safely with a pending AcceptEx} {
        set old_port [srv port]
        set new_port [find_available_port [expr {$old_port + 1}] 32]
        set sleeper [redis_deferring_client]
        set control [redis_deferring_client]
        set queued {}

        # Keep one AcceptEx completion pending while CONFIG SET port closes
        # the listener. The old RFD must not be recycled until that
        # completion has been disposed.
        $sleeper debug sleep 1
        after 100
        $control config set port $new_port
        after 50
        for {set i 0} {$i < 8} {incr i} {
            if {![catch {set s [socket [srv host] $old_port]}]} {
                lappend queued $s
            }
        }
        after 1200

        assert_equal OK [$control read]
        set new_client [redis [srv host] $new_port]
        assert_equal PONG [$new_client ping]

        assert_equal OK [r config set port $old_port]
        set old_client [redis [srv host] $old_port]
        assert_equal PONG [$old_client ping]

        foreach s $queued {catch {close $s}}
        catch {$new_client close}
        catch {$old_client close}
        catch {$sleeper close}
        catch {$control close}
    }

    test {Windows IOCP dispatches failed asynchronous ConnectEx callbacks} {
        assert_equal OK [r config set repl-timeout 60]
        set async_failures 0

        # A refused loopback connection normally completes through IOCP,
        # rather than failing the initial ConnectEx call synchronously.
        # Repeat to cover callback-close and synthetic descriptor reuse,
        # while accepting the synchronous failure form if Winsock chooses
        # it for an individual attempt.
        for {set attempt 0} {$attempt < 4} {incr attempt} {
            set dead_port [find_available_port [expr {[srv port] + 32}] 128]
            set loglines [count_log_lines 0]
            assert_equal OK [r replicaof 127.0.0.1 $dead_port]

            set match [wait_for_log_messages 0 [list \
                "*Error condition on socket for SYNC:*" \
                "*Unable to connect to MASTER:*"] $loglines 100 50]
            if {[string match "*Error condition on socket for SYNC:*" \
                              [lindex $match 0]]} {
                incr async_failures
            }

            assert_equal OK [r replicaof no one]
            wait_for_condition 50 20 {
                [lindex [r role] 0] eq {master}
            } else {
                fail "server did not leave replica mode after refused ConnectEx"
            }
            assert_equal PONG [r ping]
        }

        assert_morethan $async_failures 0
    }

    test {Windows IOCP write readiness stays bounded under backpressure} {
        set slow_pid {}
        set slow_dir {}
        set db0 {}
        set caught [catch {
            set payload_bytes [expr {1 * 1024 * 1024}]
            set reply_count 128
            set payload [string repeat x $payload_bytes]
            set db0 [redis [srv host] [srv port] 0 $::tls]
            assert_equal OK [$db0 set windows:iocp:slow-reader $payload]
            unset payload

            set slow_dir [tmpdir windows-iocp-slow-reader]
            set slow_stdout [file join $slow_dir stdout]
            set slow_stderr [file join $slow_dir stderr]
            set launch_cmd [list $::redis_test_launcher_path \
                $slow_stdout $slow_stderr -- \
                $::redis_test_launcher_path --slow-reader \
                [srv host] [srv port] windows-iocp-slow-reader \
                windows:iocp:slow-reader 4096 5000 $payload_bytes \
                $reply_count]
            set slow_pid [string trim [exec {*}$launch_cmd]]
            if {![string is wideinteger -strict $slow_pid] || $slow_pid <= 0} {
                fail "native slow-reader launcher returned an invalid PID: $slow_pid"
            }

            # Wait until Redis has installed a write handler because the
            # peer stopped draining its receive buffer.
            wait_for_condition 200 10 {
                [windows_iocp_client_has_write_event \
                    windows-iocp-slow-reader]
            } else {
                fail "slow reader never entered IOCP write-backpressure state; last client line: $::windows_iocp_client_line"
            }

            set writes_before [s total_writes_processed]
            after 500
            set writes_after [s total_writes_processed]
            set writes_delta [expr {$writes_after - $writes_before}]
            if {$writes_delta >= 100} {
                fail "backpressured client generated $writes_delta write callbacks in 500 ms"
            }
            assert_equal PONG [r ping]

            # Reading again must let the bounded retry observe real
            # Winsock writability and let the native client drain all
            # replies without a manual server-side poke.
            wait_for_condition 700 50 {
                ![process_is_alive $slow_pid]
            } else {
                fail "native slow reader did not drain and exit"
            }

            set slow_output {}
            foreach logfile [list $slow_stdout $slow_stderr] {
                if {[file exists $logfile]} {
                    set fp [open $logfile r]
                    append slow_output [read $fp]
                    close $fp
                }
            }
            assert_match \
                "*drained_replies=$reply_count bytes=$payload_bytes*" \
                $slow_output
        } result options]

        set cleanup_error {}
        if {$slow_pid ne {} && [process_is_alive $slow_pid]} {
            if {[catch {win32_kill_pid $slow_pid} cleanup_result]} {
                set cleanup_error \
                    "could not terminate native slow reader PID $slow_pid: $cleanup_result"
            } else {
                wait_for_condition 100 20 {
                    ![process_is_alive $slow_pid]
                } else {
                    set cleanup_error \
                        "native slow reader PID $slow_pid remained alive after termination"
                }
            }
        }
        if {$db0 ne {}} {
            catch {$db0 del windows:iocp:slow-reader}
            catch {$db0 close}
        }
        if {$slow_dir ne {}} { catch {file delete -force $slow_dir} }

        if {$caught} { return -options $options $result }
        if {$cleanup_error ne {}} { error $cleanup_error }
    }
}
