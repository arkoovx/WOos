BITS 64

global idt_load
global idt_stub_ignore

section .text

idt_load:
    lidt [rdi]
    ret

idt_stub_ignore:
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
