; WoOS stage2 + long mode entry
; This file handles the transition from 16-bit Real Mode to 64-bit Long Mode.

BITS 16
GLOBAL _start
EXTERN kmain

%define VBE_MODE_1280x1024x24 0x11B
%define VBE_SET_LINEAR      0x4000
%define LONG_MODE_STACK_TOP 0x00200000 ; 2MB Stack
%define BOCHS_LFB_FALLBACK  0xE0000000
%define BOOT_INFO_MAGIC     0x31424957 
%define BOOT_INFO_VERSION   0x0002
%define BOOT_INFO_E820_MAX_ENTRIES 32
%define E820_ENTRY_SIZE     24

SECTION .text.boot
_start:
    jmp entry_point

; --- CRITICAL 16-BIT REACHABLE DATA ---
; These must be in the first 64KB of the image.
ALIGN 16
boot_info:
    dd 0 ; magic
    dw 0 ; version
    dw 0 ; size
    dq 0 ; framebuffer
    dw 0 ; pitch
    dw 0 ; width
    dw 0 ; height
    db 0 ; bpp
    db 0 ; res
    dw 0 ; e820 count
    dw BOOT_INFO_E820_MAX_ENTRIES
boot_info_memory_map:
    times BOOT_INFO_E820_MAX_ENTRIES * E820_ENTRY_SIZE db 0

ALIGN 16
vbe_mode_info:
    times 256 db 0

GLOBAL gdt64
GLOBAL gdt64_descriptor

ALIGN 16
gdt64:
    dq 0x0000000000000000          ; 0x00: null
    dq 0x00CF9A000000FFFF          ; 0x08: 32-bit code
    dq 0x00CF92000000FFFF          ; 0x10: 32-bit data
    dq 0x00AF9A000000FFFF          ; 0x18: 64-bit code (L=1)
    dq 0x00AFF2000000FFFF          ; 0x20: 64-bit data Ring 3
    dq 0x00AFFA000000FFFF          ; 0x28: 64-bit code Ring 3
    dq 0x0000000000000000          ; 0x30: TSS descriptor low
    dq 0x0000000000000000          ; 0x38: TSS descriptor high
gdt64_end:

gdt64_descriptor:
    dw gdt64_end - gdt64 - 1
    dd gdt64 ; 32-bit base for LGDT in 16/32-bit mode

; Page Tables (Must be reachable for initialization)
; We only map first 1GB here to keep kernel.bin small.
ALIGN 4096
pml4_table: times 512 dq 0
ALIGN 4096
pdpt_table: times 512 dq 0
ALIGN 4096
pd_table_0: times 512 dq 0
ALIGN 4096
pd_table_1: times 512 dq 0
ALIGN 4096
pd_table_2: times 512 dq 0
ALIGN 4096
pd_table_3: times 512 dq 0

entry_point:
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov dword [boot_info + 0], BOOT_INFO_MAGIC
    mov word  [boot_info + 4], BOOT_INFO_VERSION
    mov word  [boot_info + 6], (24 + BOOT_INFO_E820_MAX_ENTRIES * 24)

    call collect_e820_map

    ; Set VBE Mode
    mov ax, 0x4F01
    mov cx, VBE_MODE_1280x1024x24
    mov di, vbe_mode_info
    int 0x10
    
    mov ax, 0x4F02
    mov bx, VBE_MODE_1280x1024x24 | VBE_SET_LINEAR
    int 0x10

    ; Fill boot_info from VBE
    mov eax, [vbe_mode_info + 0x28]
    mov [boot_info + 8], eax
    mov ax, [vbe_mode_info + 0x10]
    mov [boot_info + 16], ax
    mov ax, [vbe_mode_info + 0x12]
    mov [boot_info + 18], ax
    mov ax, [vbe_mode_info + 0x14]
    mov [boot_info + 20], ax
    mov al, [vbe_mode_info + 0x19]
    mov [boot_info + 22], al

    cli
    lgdt [gdt64_descriptor]
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp 0x08:protected_mode_start

collect_e820_map:
    xor ebx, ebx
    xor bp, bp
    mov di, boot_info_memory_map
.e820_next:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    int 0x15
    jc .e820_done
    inc bp
    add di, 24
    test ebx, ebx
    jnz .e820_next
.e820_done:
    mov [boot_info + 24], bp
    ret

SECTION .text
BITS 32
protected_mode_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Setup Paging (Identity map first 4GB)
    mov edi, pml4_table
    mov ecx, 6144 ; 6 pages * 1024 dwords = 6144 dwords = 24576 bytes
    xor eax, eax
    rep stosd

    mov eax, pdpt_table
    or eax, 0x03
    mov [pml4_table], eax

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

    mov edi, pd_table_0
    xor ebx, ebx
    mov ecx, 2048
.map_loop:
    mov eax, ebx
    or eax, 0x83 ; Present + Writable + 2MB Page
    mov [edi], eax
    add edi, 8
    add ebx, 0x200000
    loop .map_loop

    mov eax, pml4_table
    mov cr3, eax

    mov eax, cr4
    or eax, (1 << 5) | (1 << 4) ; PAE + PSE
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8 ; LME
    wrmsr

    mov eax, cr0
    or eax, (1 << 31) | (1 << 0) ; PG + PE
    mov cr0, eax

    jmp 0x18:long_mode_start

BITS 64
EXTERN __bss_start
EXTERN __bss_end

long_mode_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, LONG_MODE_STACK_TOP
    mov rbp, rsp

    ; Clear BSS
    mov rdi, __bss_start
    mov rcx, __bss_end
    sub rcx, rdi
    xor al, al
    rep stosb

    lea rdi, [rel boot_info]
    call kmain
.hang:
    hlt
    jmp .hang
