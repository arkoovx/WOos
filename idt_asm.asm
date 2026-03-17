BITS 64

global idt_load
global idt_stub_ignore
global idt_stub_irq1
global idt_stub_irq12

extern idt_handle_irq

section .text

idt_load:
    lidt [rdi]
    ret

%macro PUSH_GPRS 0
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
%endmacro

%macro POP_GPRS 0
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
%endmacro

%macro IRQ_STUB 2
%1:
    PUSH_GPRS
    mov edi, %2
    call idt_handle_irq
    POP_GPRS
    iretq
%endmacro

idt_stub_ignore:
    iretq

IRQ_STUB idt_stub_irq1, 33
IRQ_STUB idt_stub_irq12, 44

section .note.GNU-stack noalloc noexec nowrite progbits
