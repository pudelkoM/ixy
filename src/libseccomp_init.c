#include "libseccomp_init.h"

#include <unistd.h>
#include "log.h"

void setup_seccomp() {
#ifndef IXY_NO_SECCOMP
    scmp_filter_ctx ctx;
    ctx = seccomp_init(SCMP_ACT_KILL);
    if (ctx == NULL) {
        error("Failed to init seccomp filter context");
    }
    /* Add rules */
    if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getuid), 0)) {
        error("add rule");
    }
    if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(write), 1,
                         SCMP_A0(SCMP_CMP_EQ, STDOUT_FILENO))) {
        error("add rule");
    }
    if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fstat), 1,
                         SCMP_A0(SCMP_CMP_EQ, STDOUT_FILENO))) {
        error("add rule");
    }

    if (seccomp_load(ctx)) {
        error("add rule");
    }
    seccomp_release(ctx);
#endif  // IXY_NO_SECCOMP
}
