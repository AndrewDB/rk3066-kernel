#ifndef __MACH_SYSTEM_H
#define __MACH_SYSTEM_H

#include <asm/proc-fns.h>

static inline void arch_idle(void)
{
	cpu_do_idle();
}

extern void (*arch_reset)(char, const char *);

#endif
