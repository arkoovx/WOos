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

%macro IRQ_STUB_ENTER 0
    ; Полностью сохраняем прерванный контекст GPR перед вызовом C-обработчика.
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro IRQ_STUB_LEAVE 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

idt_irq1_stub:
    IRQ_STUB_ENTER
    mov dil, 33
    call idt_dispatch_irq
    IRQ_STUB_LEAVE
    iretq

idt_irq12_stub:
    IRQ_STUB_ENTER
    mov dil, 44
    call idt_dispatch_irq
    IRQ_STUB_LEAVE
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
