/* Every time the Redis Git SHA1 or Dirty status changes only this file
 * small file is recompiled, as we access this information in all the other
 * files using this functions. */

#ifdef _WIN32
/* For now hard code these version strings.
   TODO: Modify build to write them to release.h from the environment */
#define REDIS_GIT_SHA1 "00000000"
#define REDIS_GIT_DIRTY "0"
#else
#include "release.h"
#endif

char *redisGitSHA1(void) {
    return REDIS_GIT_SHA1;
}

char *redisGitDirty(void) {
    return REDIS_GIT_DIRTY;
}
