#ifndef WOOS_SYSCALL_H
#define WOOS_SYSCALL_H

#include "kernel.h"

#define SYS_READ  0
#define SYS_WRITE 1
#define SYS_EXIT  60

void syscall_init(void);
uint64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6);

#endif
