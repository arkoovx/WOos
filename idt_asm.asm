BITS 64

global idt_load
global idt_stub_ignore
global idt_stub_ignore_errcode
global idt_stub_irq0
global idt_stub_irq1
global idt_stub_irq2
global idt_stub_irq3
global idt_stub_irq4
global idt_stub_irq5
global idt_stub_irq6
global idt_stub_irq7
global idt_stub_irq8
global idt_stub_irq9
global idt_stub_irq10
global idt_stub_irq11
global idt_stub_irq12
global idt_stub_irq13
global idt_stub_irq14
global idt_stub_irq15

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
    ; IRQ может прийти в любой точке C-кода, поэтому текущий RSP
    ; не обязан соответствовать SysV ABI для вызова функции.
    ; Выравниваем стек вручную перед вызовом C-обработчика,
    ; иначе возможен #GP внутри пролога и дальнейший triple fault.
    mov r15, rsp
    and rsp, -16
    mov edi, %2
    call idt_handle_irq
    mov rsp, r15
    POP_GPRS
    iretq
%endmacro

idt_stub_ignore:
    iretq

; Для исключений, где CPU автоматически кладёт error code на стек,
; перед iretq нужно снять этот слот. Иначе iretq прочитает error code
; как RIP и спровоцирует каскад #GP/#DF до triple fault (циклический reset).
idt_stub_ignore_errcode:
    add rsp, 8
    iretq

IRQ_STUB idt_stub_irq0, 32
IRQ_STUB idt_stub_irq1, 33
IRQ_STUB idt_stub_irq2, 34
IRQ_STUB idt_stub_irq3, 35
IRQ_STUB idt_stub_irq4, 36
IRQ_STUB idt_stub_irq5, 37
IRQ_STUB idt_stub_irq6, 38
IRQ_STUB idt_stub_irq7, 39
IRQ_STUB idt_stub_irq8, 40
IRQ_STUB idt_stub_irq9, 41
IRQ_STUB idt_stub_irq10, 42
IRQ_STUB idt_stub_irq11, 43
IRQ_STUB idt_stub_irq12, 44
IRQ_STUB idt_stub_irq13, 45
IRQ_STUB idt_stub_irq14, 46
IRQ_STUB idt_stub_irq15, 47

section .note.GNU-stack noalloc noexec nowrite progbits
