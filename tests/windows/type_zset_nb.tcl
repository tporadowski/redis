# SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3
# Non-blocking zset slice. No BZPOP / extra-client / replica stream.

start_server {tags {"zset"}} {
    test {windows zset-nb: ZADD/ZCARD/ZSCORE/ZRANGE} {
        r del z
        assert_equal 3 [r zadd z 1 a 2 b 3 c]
        assert_equal 3 [r zcard z]
        assert_equal 2 [r zscore z b]
        assert_equal {a b c} [r zrange z 0 -1]
        assert_equal {c b a} [r zrevrange z 0 -1]
    }

    test {windows zset-nb: ZINCRBY/ZREM/ZRANK} {
        r del z
        r zadd z 1 a 2 b 3 c
        assert_equal 2 [r zincrby z 1 a]
        assert_equal 0 [r zrank z a]
        assert_equal 1 [r zrem z b]
        assert_equal {a c} [r zrange z 0 -1]
    }

    test {windows zset-nb: ZRANGEBYSCORE/wrong type} {
        r del z
        r zadd z 1 a 2 b 5 c
        assert_equal {a b} [r zrangebyscore z 1 3]
        assert_equal 2 [r zcount z 1 3]
        r set notaz foo
        assert_error {WRONGTYPE*} {r zadd notaz 1 x}
        assert_equal 0 [r zcard nosuch]
    }
}
