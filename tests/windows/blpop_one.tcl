# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# Tiny BLPOP extra-client check for one-shot AcceptEx. Do not source
# tests/unit/type/list.tcl: the official extra-client storm rebooted this
# machine on 2026-08-22.

start_server {tags {"list"}} {
    test {windows blpop-one: extra client unblocked by LPUSH} {
        r del mylist
        set rd [redis_deferring_client]
        $rd blpop mylist 2
        assert_equal 1 [r lpush mylist a]
        assert_equal {mylist a} [$rd read]
        $rd close
    }

    test {windows blpop-one: two waiters unblocked one by one} {
        r del mylist
        set rd1 [redis_deferring_client]
        set rd2 [redis_deferring_client]
        $rd1 blpop mylist 2
        $rd2 blpop mylist 2
        assert_equal 1 [r lpush mylist a]
        assert_equal {mylist a} [$rd1 read]
        assert_equal 1 [r lpush mylist b]
        assert_equal {mylist b} [$rd2 read]
        $rd1 close
        $rd2 close
    }
}
