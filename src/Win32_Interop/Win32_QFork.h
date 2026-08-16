/* SPDX-License-Identifier: RSALv2 OR SSPLv1 OR AGPLv3 */
#ifndef WIN32_INTEROP_QFORK_H
#define WIN32_INTEROP_QFORK_H

#ifdef _WIN32

#ifdef __cplusplus
extern "C" {
#endif

int redis_main(int argc, char **argv);

#ifndef QFORK_MAIN_IMPL
#define main redis_main
#endif

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */
#endif
