/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_QFORK_IMPL_H
#define WIN32_QFORK_IMPL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Win32QForkJob {
    int purpose;
    int rdb_subtype;
    int rdb_req;
    int rdb_flags;
    int rsi_valid;
    char filename[260];
    unsigned char rsi[80];
    int rdb_channel;
    int slots_req;
    int numconns;
    void **conns; /* parent connection*[]; valid in parent only */
    int rdb_pipe_write;
    int safe_to_exit_pipe;
    char module_symbol[64];
    void *module_user_data;
} Win32QForkJob;

extern Win32QForkJob g_win32_qfork_job;

void SetupRedisGlobals(void *redisData, size_t redisDataSize,
                       unsigned char *dictHashSeed, int purpose);
int do_rdbSave(int req, char *filename, void *rsi, int rdbflags);
int do_rdbSaveToSockets(int req, void *rsi, void **conns, int numconns,
                        int use_conns, int rdb_pipe_write, int safe_to_exit);
int do_rdbSaveToSocketsChild(QForkPayloadHeader *hdr, void *proto_blob);
int do_aofRewrite(const char *filename);
int do_moduleFork(const char *path, const char *symbol, void *user_data);
void win32PrepareAofJob(void);
void win32ApplyPersistenceAvailable(int available);

#ifdef __cplusplus
}
#endif

#endif
