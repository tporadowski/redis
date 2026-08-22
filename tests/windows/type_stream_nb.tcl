# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# Non-blocking stream slice. No XREAD BLOCK / extra-client.

start_server {tags {"stream"}} {
    test {windows stream-nb: XADD/XLEN/XRANGE} {
        r del s
        set id1 [r xadd s * a 1]
        set id2 [r xadd s * a 2]
        assert_equal 2 [r xlen s]
        set entries [r xrange s - +]
        assert_equal 2 [llength $entries]
        assert_equal $id1 [lindex $entries 0 0]
        assert_equal $id2 [lindex $entries 1 0]
    }

    test {windows stream-nb: XREVRANGE/XDEL} {
        r del s
        r xadd s 1-0 a 1
        r xadd s 2-0 a 2
        r xadd s 3-0 a 3
        assert_equal {3-0} [lindex [r xrevrange s + - COUNT 1] 0 0]
        assert_equal 1 [r xdel s 2-0]
        assert_equal 2 [r xlen s]
    }

    test {windows stream-nb: XTRIM/wrong type} {
        r del s
        r xadd s 1-0 a 1
        r xadd s 2-0 a 2
        r xadd s 3-0 a 3
        assert_equal 1 [r xtrim s maxlen 2]
        assert_equal 2 [r xlen s]
        r set notastream foo
        assert_error {WRONGTYPE*} {r xadd notastream * a 1}
        assert_equal 0 [r xlen nosuch]
    }
}
