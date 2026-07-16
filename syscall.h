#ifndef WOOS_SYSCALL_H
#define WOOS_SYSCALL_H

#include "kernel.h"

#include "sched.h"

#define SYS_READ  0
#define SYS_WRITE 1
#define SYS_EXIT  60

void syscall_init(void);
uint64_t syscall_handler(context_t* ctx);

#endif
