#ifndef IXY_SECCOMP_H
#define IXY_SECCOMP_H

#include <unistd.h>
#include <seccomp.h>

#include "log.h"

static inline void setup_seccomp() {
	scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL);
	if (ctx == NULL) {
		error("Failed to init seccomp filter context");
	}

	/* Allow output printing to stdout and stderr */
	if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(write), 1,
						 SCMP_A0(SCMP_CMP_EQ, STDOUT_FILENO))) {
		error("Failed to add rule");
	}
	if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(write), 1,
						 SCMP_A0(SCMP_CMP_EQ, STDERR_FILENO))) {
		error("Failed to add rule");
	}

	/* Allow access to io port fds */
	if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(pwrite64), 1,
						 SCMP_A0(SCMP_CMP_GE, 0))) {
		error("Failed to add rule");		
	}
	if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(pread64), 1,
						 SCMP_A0(SCMP_CMP_GE, 0))) {
		error("Failed to add rule");		
	}

	if (seccomp_load(ctx)) {
		error("Failed to load seccomp context into kernel");
	}
	seccomp_release(ctx);
}

#endif // IXY_SECCOMP_H
