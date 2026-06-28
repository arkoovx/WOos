#include "syscall.h"
#include "serial.h"

void syscall_init(void) {
    serial_printf("[Syscall] Syscall interface initialized (int 0x80).\n");
}

uint64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6) {
    (void)arg4; (void)arg5; (void)arg6;
    switch (num) {
        case SYS_WRITE: {
            int fd = (int)arg1;
            const char* buf = (const char*)arg2;
            size_t count = (size_t)arg3;
            if (fd == 1 || fd == 2) {
                for (size_t i = 0; i < count; i++) {
                    serial_write_char(buf[i]);
                }
                return count;
            }
            return -1;
        }
        case SYS_EXIT: {
            serial_printf("[Syscall] Process exited with status %d\n", (int)arg1);
            while(1) {
                __asm__ __volatile__("hlt");
            }
            return 0;
        }
        default:
            serial_printf("[Syscall] Unknown syscall %d\n", (int)num);
            return -1;
    }
}
