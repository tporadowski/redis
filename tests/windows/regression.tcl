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
