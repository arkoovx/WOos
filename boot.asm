; WoOS: BIOS boot path to x86_64 Long Mode
; stage1 (first 512 bytes) loads stage2, stage2 does VBE + paging + LM jump.

BITS 16

SECTION .boot_sector
GLOBAL boot_start
EXTERN kmain

%define VBE_MODE_1024x768x32 0x118
%define VBE_SET_LINEAR      0x4000
%define STAGE2_SECTORS      64

boot_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl

    ; Read stage2 from disk into 0000:8000.
    mov si, disk_address_packet
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc boot_fail

    jmp _start

boot_fail:
    hlt
    jmp boot_fail

boot_drive: db 0

disk_address_packet:
    db 0x10
    db 0x00
    dw STAGE2_SECTORS
    dw 0x8000
    dw 0x0000
    dq 2

; BIOS signature at bytes 510..511 of sector 0.
TIMES 510 - ($ - $$) db 0
DW 0xAA55

SECTION .stage2_entry
BITS 16
GLOBAL _start
_start:
    ; -----------------------------------------------------------------
    ; VBE setup in real mode. We request 1024x768 with LFB and store
    ; framebuffer metadata for the 64-bit kernel entry point.
    ; -----------------------------------------------------------------
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

    ; Load 64-bit capable GDT and enter protected mode first.
    lgdt [gdt64_descriptor]
    mov eax, cr0
    or eax, 0x1
    mov cr0, eax
    jmp 0x08:protected_mode_start

stage2_fail:
    hlt
    jmp stage2_fail

SECTION .stage2

BITS 32
protected_mode_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; -----------------------------------------------------------------
    ; Paging bootstrap for Long Mode:
    ; 1) Zero PML4, PDPT, PD tables (4 KiB each, aligned to 4 KiB).
    ; 2) PML4[0] -> PDPT with Present+RW.
    ; 3) PDPT[0] -> PD with Present+RW.
    ; 4) PD[0]   -> 2 MiB huge identity page (PS=1, Present+RW).
    ;
    ; This identity-map keeps current physical addresses valid immediately
    ; after CR0.PG=1. Without this, CPU fetches invalid instructions and
    ; resets with a triple fault.
    ; -----------------------------------------------------------------
    xor eax, eax
    mov edi, pml4_table
    mov ecx, (4096 * 3) / 4
    rep stosd

    mov eax, pdpt_table
    or eax, 0x03
    mov [pml4_table], eax

    mov eax, page_directory
    or eax, 0x03
    mov [pdpt_table], eax

    mov dword [page_directory], 0x00000083
    mov dword [page_directory + 4], 0x00000000

    mov eax, pml4_table
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5                 ; CR4.PAE
    mov cr4, eax

    mov ecx, 0xC0000080            ; IA32_EFER
    rdmsr
    or eax, 1 << 8                 ; EFER.LME
    wrmsr

    mov eax, cr0
    or eax, (1 << 31) | (1 << 0)   ; CR0.PG | CR0.PE
    mov cr0, eax

    jmp 0x08:long_mode_start

BITS 64
long_mode_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov rsp, 0x90000
    mov rbp, rsp

    lea rdi, [rel boot_info]
    call kmain

.hang:
    hlt
    jmp .hang

ALIGN 16
boot_info:
    dq 0          ; framebuffer (u64)
    dw 0          ; pitch
    dw 0          ; width
    dw 0          ; height
    db 0          ; bpp
    db 0          ; reserved

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
page_directory:
    times 512 dq 0
