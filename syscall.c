#include "syscall.h"
#include "serial.h"
#include "sched.h"

// Ассемблерная точка входа
extern void syscall_entry(void);

// Вспомогательные функции для чтения/записи MSR
static inline uint64_t read_msr(uint32_t msr) {
    uint32_t low, high;
    __asm__ __volatile__("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void write_msr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

void syscall_init(void) {
    // 1. Включаем SCE (System Call Extensions) в IA32_EFER (MSR 0xC0000080)
    uint64_t efer = read_msr(0xC0000080);
    write_msr(0xC0000080, efer | 1ULL);

    // 2. Настраиваем сегментные селекторы в IA32_STAR (MSR 0xC0000081)
    // STAR[47:32] = 0x18 (Kernel Code CS=0x18, SS=0x20)
    // STAR[63:48] = 0x20 (User base selector for SYSRET: CS=0x33, SS=0x23)
    uint64_t star = 0;
    star |= (0x18ULL << 32); // Kernel CS/SS base
    star |= (0x20ULL << 48); // User CS/SS base
    write_msr(0xC0000081, star);

    // 3. Записываем адрес syscall_entry в IA32_LSTAR (MSR 0xC0000082)
    write_msr(0xC0000082, (uint64_t)syscall_entry);

    // 4. Сбрасываем флаг прерываний IF (0x200) и направление DF (0x400) при входе через FMASK
    write_msr(0xC0000084, 0x200ULL | 0x400ULL);

    serial_printf("[Syscall] Syscall interface initialized (SYSCALL/SYSRET).\n");
}

uint64_t syscall_handler(context_t* ctx) {
    uint64_t num = ctx->rax;
    uint64_t arg1 = ctx->rdi;
    uint64_t arg2 = ctx->rsi;
    uint64_t arg3 = ctx->rdx;

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
            thread_exit((int)arg1);
            return 0;
        }
        default:
            serial_printf("[Syscall] Unknown syscall %d\n", (int)num);
            return -1;
    }
}
