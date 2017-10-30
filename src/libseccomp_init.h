#ifndef IXY_LIBSECCOMP_INIT_H
#define IXY_LIBSECCOMP_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

// Optional disable switch
#ifndef IXY_NO_SECCOMP
#include <seccomp.h>
#else
#warning SECCOMP disabled, proceed with caution
#endif //IXY_NO_SECCOMP

void setup_seccomp();

#ifdef __cplusplus
}
#endif

#endif //IXY_LIBSECCOMP_INIT_H
