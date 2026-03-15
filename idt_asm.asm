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

%macro IRQ_STUB 2
%1:
    ; IRQ приходит асинхронно к любому C-коду, поэтому сохраняем все GPR,
    ; чтобы гарантированно не повредить состояние прерванного потока.
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov dil, %2
    call idt_dispatch_irq

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    iretq
%endmacro

IRQ_STUB idt_irq1_stub, 33
IRQ_STUB idt_irq12_stub, 44

section .note.GNU-stack noalloc noexec nowrite progbits
