# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# Lifted from tporadowski 5.0 tests/windows/regression.tcl (AUTH replica
# sync + maxclients after a failed replica AUTH). slaveof is still an
# alias of replicaof on 8.10.

proc log_file_matches {log pattern} {
    set fp [open $log r]
    set content [read $fp]
    close $fp
    string match $pattern $content
}

start_server {tags {"regression"}} {
    set replica [srv 0 client]
    set replica_host [srv 0 host]
    set replica_port [srv 0 port]
    set replica_log [srv 0 stdout]
    start_server {} {
        set master [srv 0 client]
        set master_host [srv 0 host]
        set master_port [srv 0 port]

        $master config set requirepass mypwd
        $replica config set masterauth mypwd

        $replica replicaof $master_host $master_port

        test {Replica is able to sync with master when AUTH is on} {
            wait_for_condition 50 100 {
                [log_file_matches $replica_log "*Finished with success*"]
            } else {
                fail "Replica is not able to sync with master when AUTH is on"
            }
        }
    }
}

start_server {tags {"regression"}} {
    set A [srv 0 client]
    set A_host [srv 0 host]
    set A_port [srv 0 port]

    set max_clients 5
    set arg [format {overrides {maxclients %d requirepass foobar}} $max_clients]
    start_server $arg {
        set B [srv 0 client]
        set B_host [srv 0 host]
        set B_port [srv 0 port]

        $A replicaof $B_host $B_port

        test {Master should release the connection after an AUTH failure from a Replica} {
            wait_for_condition 50 100 {
                [lindex [$A role] 0] eq {slave}
            } else {
                fail {"Can't turn the instance into a replica"}
            }

            after 5000

            r auth foobar
            set client_count 0
            set client_list [r client list]
            foreach item $client_list {
                if {[string match "id=*" $item]} {
                    incr client_count
                }
            }
            assert {$client_count < $max_clients}
        }
    }
}

# Cluster config lock + IPv6 protected-mode. Not tagged "cluster": the default
# wintest deny would skip the block, and this is one server, not the cluster suite.
# Do not port pwin32's "io-threads are unsupported" case; this port runs them.

proc windows_forward_slash_path {path} {
    string map [list \\ /] [file nativename [file normalize $path]]
}

proc windows_cluster_lock_contender {cluster_config_file} {
    set parent_config [dict get [lindex $::servers end] config]
    set contender_dir [tmpdir cluster-lock-contender]
    set contender_config_file [tmpfile cluster-lock-contender.conf]
    set contender_stdout [file join $contender_dir stdout]
    set contender_stderr [file join $contender_dir stderr]
    set contender_port [find_available_port $::baseport $::portcount]

    set contender_config $parent_config
    dict set contender_config dir [windows_forward_slash_path $contender_dir]
    dict set contender_config port $contender_port
    dict set contender_config cluster-config-file $cluster_config_file
    dict unset contender_config unixsocket
    create_server_config_file $contender_config_file $contender_config {}

    set pid {}
    set caught [catch {
        set launch_cmd [list $::redis_test_launcher_path \
            $contender_stdout $contender_stderr -- \
            $::redis_server_path $contender_config_file]
        set pid [string trim [exec {*}$launch_cmd]]
        if {![string is wideinteger -strict $pid] || $pid <= 0} {
            error "hidden Redis launcher returned an invalid PID: $pid"
        }

        set exited 0
        # QFork heap setup can delay this deliberately failing process.
        for {set attempt 0} {$attempt < 1200} {incr attempt} {
            if {![process_is_alive $pid]} {
                set exited 1
                break
            }
            after 100
        }
        if {!$exited} {
            fail "second Redis process did not reject the locked cluster configuration"
        }

        set output {}
        foreach logfile [list $contender_stdout $contender_stderr] {
            if {[file exists $logfile]} {
                set fp [open $logfile r]
                append output [read $fp]
                close $fp
            }
        }
        assert_match \
            {*already used by a different Redis Cluster node*} $output
    } result options]

    if {$pid ne {} && [process_is_alive $pid]} {
        if {![win32_process_matches $pid]} {
            error "refusing to terminate unexpected contender PID $pid"
        }
        win32_kill_pid $pid
        wait_for_condition 100 20 {
            ![process_is_alive $pid]
        } else {
            error "cluster lock contender PID $pid did not exit"
        }
    }
    catch {file delete -force $contender_config_file $contender_dir}

    if {$caught} {
        return -options $options $result
    }
}

set old_singledb $::singledb
set ::singledb 1
set cluster_config_dir [windows_forward_slash_path [tmpdir cluster-config-replacement]]
set cluster_config_file [windows_forward_slash_path \
    [file join $cluster_config_dir nodes.conf]]
start_server [list \
    tags {windows regression external:skip tls:skip} \
    overrides [list \
        cluster-enabled yes \
        cluster-node-timeout 1000 \
        cluster-config-file $cluster_config_file]] {
    test {Windows atomically replaces and continuously locks the cluster configuration} {
        set node_id [r cluster myid]
        assert_match {BUMPED *} [r cluster bumpepoch]

        for {set save 0} {$save < 20} {incr save} {
            assert_equal OK [r cluster saveconfig]
        }

        assert_equal 1 [file exists $cluster_config_file]
        assert_equal 1 [file exists ${cluster_config_file}.lock]
        assert_morethan [file size $cluster_config_file] 0

        set fp [open $cluster_config_file r]
        set cluster_config [read $fp]
        close $fp
        assert_match "*$node_id*" $cluster_config
        assert_match {*vars currentEpoch *} $cluster_config
        assert_equal {} [glob -nocomplain ${cluster_config_file}.tmp-*]

        # Start the contender only after repeated atomic replacements.
        # This catches a Windows lock that remained attached to an old,
        # replaced nodes.conf object rather than to the live path.
        windows_cluster_lock_contender $cluster_config_file
        assert_equal PONG [r ping]
        assert_equal OK [r cluster saveconfig]

        # A graceful shutdown must release the stable companion lock.
        # Restarting from the same nodes.conf also validates that the
        # replaced configuration remains parseable and preserves MYID.
        restart_server 0 true false
        assert_equal $node_id [r cluster myid]
        assert_equal OK [r cluster saveconfig]
        assert_equal {} [glob -nocomplain ${cluster_config_file}.tmp-*]
    }
}
catch {file delete -force $cluster_config_dir}
set ::singledb $old_singledb

start_server {tags {"regression network external:skip tls:skip"} omit {bind}} {
    test {Protected mode accepts IPv6 loopback through IOCP} {
        set replies {}
        for {set i 0} {$i < 2} {incr i} {
            set c [redis ::1 [srv 0 port]]
            set failed [catch {
                set pong [$c ping]
                set info [$c client info]
                if {![regexp {addr=\[::1\]:[0-9]+} $info]} {
                    error "unexpected IPv6 client address: $info"
                }
                set pong
            } reply]
            catch {$c close}
            if {$failed} {error $reply}
            lappend replies $reply
        }
        set replies
    } {PONG PONG}

    test {Windows rearms the background I/O completion pipe after FLUSHDB} {
        r mset a 1 b 2 c 3 d 4 e 5 f 6 g 7 h 8 i 9 j 10
        assert_equal OK [r flushdb]
        r set after-flush value
        assert_equal OK [r flushdb]
        assert_equal 0 [r dbsize]
        assert_equal PONG [r ping]
    }
}

