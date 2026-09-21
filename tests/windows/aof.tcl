# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# QFork initial AOF rewrite: turning appendonly off must drop the uncommitted
# temp INCR. Adapted from pwin32 tests/windows/aof.tcl.

start_server {tags {"windows aof qfork regression external:skip tls:skip needs:debug"} overrides {appendonly no save "" aof-use-rdb-preamble yes}} {
    test {Windows disabling AOF during initial QFork rewrite removes the temporary INCR} {
        set dir [get_redis_dir]
        set appenddirname [lindex [r config get appenddirname] 1]
        set appendfilename [lindex [r config get appendfilename] 1]
        set aof_dir [file join $dir $appenddirname]
        set temp_incr [file join $aof_dir temp-${appendfilename}.incr]
        set manifest [file join $aof_dir ${appendfilename}.manifest]

        r debug populate 100 aof-stop 1024
        r config set rdb-key-save-delay 100000
        r config set appendonly yes

        wait_for_condition 100 20 {
            [s aof_rewrite_in_progress] == 1 && [file exists $temp_incr]
        } else {
            fail "initial AOF rewrite did not create its temporary INCR"
        }

        r set written-during-initial-rewrite yes
        r config set appendonly no

        assert_equal 0 [s aof_enabled]
        assert_equal 0 [s aof_rewrite_in_progress]
        assert_equal 0 [file exists $temp_incr]
        assert_equal 0 [file exists $manifest]

        r config set rdb-key-save-delay 0
        r config set appendonly yes
        waitForBgrewriteaof r
        r set written-after-reenable yes

        set before [r debug digest]
        r debug loadaof
        set after [r debug digest]
        assert_equal $before $after

        r config set appendonly no
    }
}
