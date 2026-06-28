BITS 64

global idt_load
global idt_stub_ignore
global idt_stub_ignore_errcode
global idt_stub_syscall
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
extern idt_handle_exception

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

extern syscall_handler
idt_stub_syscall:
    PUSH_GPRS
    
    ; Setup arguments for syscall_handler:
    ; rdi = rax (num)
    ; rsi = rdi (arg1)
    ; rdx = rsi (arg2)
    ; rcx = rdx (arg3)
    ; r8  = r10 (arg4)
    ; r9  = r8  (arg5)
    ; Stack: r9 (arg6)
    push r9
    mov r9, r8
    mov r8, r10
    mov rcx, rdx
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax

    mov r15, rsp
    and rsp, -16
    sub rsp, 8
    
    call syscall_handler
    
    mov rsp, r15
    add rsp, 8 ; clean up r9 from stack
    
    ; Overwrite saved rax with the return value
    mov [rsp + 112], rax
    
    POP_GPRS
    iretq

%macro IRQ_STUB 2
%1:
    PUSH_GPRS
    ; IRQ может прийти в любой точке C-кода, поэтому текущий RSP
    ; не обязан соответствовать SysV ABI для вызова функции.
    ; Выравниваем стек вручную перед вызовом C-обработчика,
    ; иначе возможен #GP внутри пролога и дальнейший triple fault.
    mov r15, rsp
    and rsp, -16
    ; Для SysV ABI перед call нужно состояние rsp % 16 == 8,
    ; чтобы в теле C-функции стек был выровнен на 16 байт.
    sub rsp, 8
    mov edi, %2
    call idt_handle_irq
    mov rsp, r15
    POP_GPRS
    iretq
%endmacro

; Макрос для исключений БЕЗ кода ошибки (кладёт фиктивный 0 на стек)
%macro EXCEPTION_NOERRCODE 2
global idt_stub_exception%1
idt_stub_exception%1:
    push qword 0         ; фиктивный error code
    push qword %2        ; вектор исключения
    PUSH_GPRS
    mov rdi, rsp         ; указатель на registers_t
    mov r15, rsp
    and rsp, -16
    sub rsp, 8
    call idt_handle_exception
    mov rsp, r15
    POP_GPRS
    add rsp, 16          ; очищаем вектор и error code
    iretq
%endmacro

; Макрос для исключений С кодом ошибки
%macro EXCEPTION_ERRCODE 2
global idt_stub_exception%1
idt_stub_exception%1:
    ; CPU уже положил error code на стек
    push qword %2        ; вектор исключения
    PUSH_GPRS
    mov rdi, rsp         ; указатель на registers_t
    mov r15, rsp
    and rsp, -16
    sub rsp, 8
    call idt_handle_exception
    mov rsp, r15
    POP_GPRS
    add rsp, 16          ; очищаем вектор и error code
    iretq
%endmacro

; Описание всех 32 исключений CPU
EXCEPTION_NOERRCODE 0, 0
EXCEPTION_NOERRCODE 1, 1
EXCEPTION_NOERRCODE 2, 2
EXCEPTION_NOERRCODE 3, 3
EXCEPTION_NOERRCODE 4, 4
EXCEPTION_NOERRCODE 5, 5
EXCEPTION_NOERRCODE 6, 6
EXCEPTION_NOERRCODE 7, 7
EXCEPTION_ERRCODE   8, 8
EXCEPTION_NOERRCODE 9, 9
EXCEPTION_ERRCODE   10, 10
EXCEPTION_ERRCODE   11, 11
EXCEPTION_ERRCODE   12, 12
EXCEPTION_ERRCODE   13, 13
EXCEPTION_ERRCODE   14, 14
EXCEPTION_NOERRCODE 15, 15
EXCEPTION_NOERRCODE 16, 16
EXCEPTION_ERRCODE   17, 17
EXCEPTION_NOERRCODE 18, 18
EXCEPTION_NOERRCODE 19, 19
EXCEPTION_NOERRCODE 20, 20
EXCEPTION_ERRCODE   21, 21
EXCEPTION_NOERRCODE 22, 22
EXCEPTION_NOERRCODE 23, 23
EXCEPTION_NOERRCODE 24, 24
EXCEPTION_NOERRCODE 25, 25
EXCEPTION_NOERRCODE 26, 26
EXCEPTION_NOERRCODE 27, 27
EXCEPTION_NOERRCODE 28, 28
EXCEPTION_ERRCODE   29, 29
EXCEPTION_ERRCODE   30, 30
EXCEPTION_NOERRCODE 31, 31

idt_stub_ignore:
    iretq

; Для исключений, где CPU автоматически кладёт error code на стек,
; перед iretq нужно снять этот слот. Иначе iretq прочитает error code
; как RIP и спровоцирует каскад #GP/#DF до triple fault (циклический reset).
idt_stub_ignore_errcode:
    add rsp, 8
    iretq

global idt_stub_irq0
idt_stub_irq0:
    PUSH_GPRS
    mov rdi, rsp
    mov r15, rsp
    and rsp, -16
    sub rsp, 8
    
    extern schedule_interrupt
    call schedule_interrupt
    
    test rax, rax
    jz .no_switch
    mov rsp, rax
    jmp .done
.no_switch:
    mov rsp, r15
.done:
    POP_GPRS
    iretq

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
