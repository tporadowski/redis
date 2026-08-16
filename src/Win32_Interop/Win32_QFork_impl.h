/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_QFORK_IMPL_H
#define WIN32_QFORK_IMPL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Win32QForkJob {
    int rdb_req;
    int rdb_flags;
    int rsi_valid;
    char filename[260];
    unsigned char rsi[80];
} Win32QForkJob;

extern Win32QForkJob g_win32_qfork_job;

void SetupRedisGlobals(void *redisData, size_t redisDataSize, unsigned char *dictHashSeed);
int do_rdbSave(int req, char *filename, void *rsi, int rdbflags);
void win32ApplyPersistenceAvailable(int available);

#ifdef __cplusplus
}
#endif

#endif
