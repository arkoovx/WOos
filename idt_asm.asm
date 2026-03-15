BITS 64

global idt_load
global idt_stub_ignore
global idt_irq1_stub
global idt_irq12_stub

extern idt_dispatch_irq

section .text

idt_load:
    lidt [rdi]
    ret

idt_stub_ignore:
    iretq

idt_irq1_stub:
    push rax
    mov dil, 33
    call idt_dispatch_irq
    pop rax
    iretq

idt_irq12_stub:
    push rax
    mov dil, 44
    call idt_dispatch_irq
    pop rax
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
