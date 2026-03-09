; WoOS stage2 + long mode entry

BITS 16
GLOBAL _start
EXTERN kmain

%define VBE_MODE_1024x768x32 0x118
%define VBE_SET_LINEAR      0x4000
%define LONG_MODE_STACK_TOP 0x0009F000

SECTION .text.boot
_start:
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax

    mov ax, 0x4F01
    mov cx, VBE_MODE_1024x768x32
    mov di, vbe_mode_info
    int 0x10
    cmp ax, 0x004F
    jne stage2_fail

    mov ax, 0x4F02
    mov bx, VBE_MODE_1024x768x32 | VBE_SET_LINEAR
    int 0x10
    cmp ax, 0x004F
    jne stage2_fail

    mov eax, [vbe_mode_info + 0x28]
    mov [boot_info + 0], eax
    mov ax, [vbe_mode_info + 0x10]
    mov [boot_info + 8], ax
    mov ax, [vbe_mode_info + 0x12]
    mov [boot_info + 10], ax
    mov ax, [vbe_mode_info + 0x14]
    mov [boot_info + 12], ax
    mov al, [vbe_mode_info + 0x19]
    mov [boot_info + 14], al

    cli
    lgdt [gdt64_descriptor]
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp 0x08:protected_mode_start

stage2_fail:
    hlt
    jmp stage2_fail

SECTION .text
BITS 32
protected_mode_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    xor eax, eax
    mov edi, pml4_table
    mov ecx, (4096 * 2) / 4
    rep stosd

    mov eax, pdpt_table
    or eax, 0x03
    mov [pml4_table], eax

    ; Identity-map low 4 GiB using 1 GiB pages so VBE LFB addresses are valid.
    mov dword [pdpt_table + 0], 0x00000083
    mov dword [pdpt_table + 4], 0x00000000
    mov dword [pdpt_table + 8], 0x40000083
    mov dword [pdpt_table + 12], 0x00000000
    mov dword [pdpt_table + 16], 0x80000083
    mov dword [pdpt_table + 20], 0x00000000
    mov dword [pdpt_table + 24], 0xC0000083
    mov dword [pdpt_table + 28], 0x00000000

    mov eax, pml4_table
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, (1 << 31) | (1 << 0)
    mov cr0, eax

    jmp 0x08:long_mode_start

BITS 64
long_mode_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Keep the stack below VGA memory (0xA0000) and away from stage2/kernel image
    ; loaded at 0x8000.
    mov rsp, LONG_MODE_STACK_TOP
    mov rbp, rsp

    lea rdi, [rel boot_info]
    call kmain

.hang:
    hlt
    jmp .hang

ALIGN 16
boot_info:
    dq 0
    dw 0
    dw 0
    dw 0
    db 0
    db 0

ALIGN 16
vbe_mode_info:
    times 256 db 0

ALIGN 16
gdt64:
    dq 0x0000000000000000
    dq 0x00AF9A000000FFFF
    dq 0x00AF92000000FFFF

gdt64_descriptor:
    dw gdt64_descriptor - gdt64 - 1
    dq gdt64

SECTION .paging ALIGN=4096
ALIGN 4096
pml4_table:
    times 512 dq 0
ALIGN 4096
pdpt_table:
    times 512 dq 0
