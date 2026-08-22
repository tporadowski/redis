# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# Non-blocking set slice. Do not source tests/unit/type/set.tcl: it
# builds a 100k-member set and kill -9s a QFork child.

start_server {tags {"set"}} {
    test {windows set-nb: SADD/SCARD/SISMEMBER/SMEMBERS} {
        r del myset
        assert_equal 3 [r sadd myset a b c]
        assert_equal 0 [r sadd myset a]
        assert_equal 3 [r scard myset]
        assert_equal 1 [r sismember myset b]
        assert_equal 0 [r sismember myset z]
        assert_equal {a b c} [lsort [r smembers myset]]
    }

    test {windows set-nb: SREM/SMOVE/SUNION} {
        r del a{t} b{t}
        r sadd a{t} 1 2 3
        r sadd b{t} 3 4
        assert_equal 1 [r srem a{t} 1]
        assert_equal 1 [r smove a{t} b{t} 2]
        assert_equal {3} [lsort [r smembers a{t}]]
        assert_equal {2 3 4} [lsort [r smembers b{t}]]
        assert_equal {2 3 4} [lsort [r sunion a{t} b{t}]]
    }

    test {windows set-nb: SINTER/SDIFF/wrong type} {
        r del a{t} b{t}
        r sadd a{t} 1 2 3
        r sadd b{t} 2 3 4
        assert_equal {2 3} [lsort [r sinter a{t} b{t}]]
        assert_equal {1} [r sdiff a{t} b{t}]
        r set notaset foo
        assert_error {WRONGTYPE*} {r sadd notaset x}
        assert_equal 0 [r scard nosuch]
    }
}
