/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
/* 6.3 smoke: RedisModule_Fork without SetForkChildFn must return -1. */
#include "../redismodule.h"

int ForkCheckOk_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    RedisModule_ReplyWithLongLong(ctx, 1);
    return REDISMODULE_OK;
}

int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);
    if (RedisModule_Init(ctx, "forkcheck", 1, REDISMODULE_APIVER_1)
        == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    int pid = RedisModule_Fork(NULL, NULL);
    if (pid != -1)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx, "forkcheck.ok",
            ForkCheckOk_RedisCommand, "readonly", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    return REDISMODULE_OK;
}
