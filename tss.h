#ifndef WOOS_TSS_H
#define WOOS_TSS_H

#include "kernel.h"

typedef struct tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb;
} __attribute__((packed)) tss_t;

void tss_init(void* kernel_stack_top);
void tss_set_rsp0(uint64_t rsp0);

#endif
