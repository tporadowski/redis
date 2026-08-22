# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# Non-blocking list slice. Do not source tests/unit/type/list.tcl here:
# that file's run_solo list-large-memory starts a server and tries 10GB
# configs even when --only skips the 4GB tests. Extra-client BLPOP
# rebooted this machine on 2026-08-22.

start_server {tags {"list"}} {
    test {windows list-nb: LPUSH/RPUSH/LLEN/LINDEX} {
        r del mylist
        assert_equal 2 [r lpush mylist b a]
        assert_equal 4 [r rpush mylist c d]
        assert_equal 4 [r llen mylist]
        assert_equal a [r lindex mylist 0]
        assert_equal d [r lindex mylist -1]
        assert_equal {a b c d} [r lrange mylist 0 -1]
    }

    test {windows list-nb: LSET/LREM/LTRIM} {
        r del mylist
        r rpush mylist a b c d e
        assert_equal OK [r lset mylist 1 x]
        assert_equal {a x c d e} [r lrange mylist 0 -1]
        assert_equal 1 [r lrem mylist 1 x]
        assert_equal {a c d e} [r lrange mylist 0 -1]
        assert_equal OK [r ltrim mylist 1 2]
        assert_equal {c d} [r lrange mylist 0 -1]
    }

    test {windows list-nb: LINSERT/LPUSHX/RPOP} {
        r del mylist
        assert_equal 0 [r lpushx mylist z]
        r rpush mylist a c
        assert_equal 3 [r linsert mylist before c b]
        assert_equal {a b c} [r lrange mylist 0 -1]
        assert_equal c [r rpop mylist]
        assert_equal {a b} [r lrange mylist 0 -1]
    }

    test {windows list-nb: wrong-type and missing key} {
        r del mylist
        assert_equal 0 [r llen notalist]
        assert_equal {} [r lindex notalist 0]
        assert_equal {} [r lrange notalist 0 1]
        r set notalist foo
        assert_error {WRONGTYPE*} {r llen notalist}
        assert_error {WRONGTYPE*} {r lindex notalist 0}
        assert_error {WRONGTYPE*} {r lset notalist 0 x}
        assert_error {ERR*} {r lset nosuch 0 x}
    }
}
