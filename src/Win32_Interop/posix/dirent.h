/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_POSIX_DIRENT_H
#define WIN32_POSIX_DIRENT_H

#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#endif

struct dirent {
    unsigned char d_type;
    char d_name[260];
};

typedef struct DIR DIR;

#ifdef __cplusplus
extern "C" {
#endif
DIR *opendir(const char *name);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);
#ifdef __cplusplus
}
#endif

#endif
