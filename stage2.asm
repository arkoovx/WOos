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
    mov ecx, (4096 * 6) / 4
    rep stosd

    mov eax, pdpt_table
    or eax, 0x03
    mov [pml4_table], eax

    ; Identity-map low 4 GiB using 2 MiB pages. This avoids relying on 1 GiB
    ; page support, which is not guaranteed on every virtual CPU model.
    mov eax, pd_table_0
    or eax, 0x03
    mov [pdpt_table + 0], eax

    mov eax, pd_table_1
    or eax, 0x03
    mov [pdpt_table + 8], eax

    mov eax, pd_table_2
    or eax, 0x03
    mov [pdpt_table + 16], eax

    mov eax, pd_table_3
    or eax, 0x03
    mov [pdpt_table + 24], eax

    ; Fill 4 page directories * 512 entries with 2 MiB identity mappings.
    mov edi, pd_table_0
    xor ebx, ebx
    mov ecx, 2048
.map_2m_loop:
    mov eax, ebx
    or eax, 0x83                ; present + writable + page size (2 MiB)
    mov [edi], eax
    mov dword [edi + 4], 0
    add ebx, 0x200000
    add edi, 8
    loop .map_2m_loop

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
ALIGN 4096
pd_table_0:
    times 512 dq 0
ALIGN 4096
pd_table_1:
    times 512 dq 0
ALIGN 4096
pd_table_2:
    times 512 dq 0
ALIGN 4096
pd_table_3:
    times 512 dq 0
