; WoOS boot sector (stage 1, real mode only)
; Reads stage2+kernel payload from disk to 0000:8000 and jumps there.

BITS 16
ORG 0x7C00

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 64
%endif

%define LOAD_SEGMENT 0x0000
%define LOAD_OFFSET  0x8000
%define RESET_RETRIES 5

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl

    mov byte [retry_count], RESET_RETRIES
.reset_disk:
    xor ax, ax          ; AH=0 int13 reset
    mov dl, [boot_drive]
    int 0x13
    jnc .read_payload
    dec byte [retry_count]
    jnz .reset_disk
    jmp disk_error

.read_payload:
    mov ax, LOAD_SEGMENT
    mov es, ax
    mov bx, LOAD_OFFSET

    ; INT 13h extensions (AH=42h) are used so large payloads load reliably
    ; regardless of disk geometry.
    mov si, disk_address_packet
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    jmp LOAD_SEGMENT:LOAD_OFFSET

disk_error:
    mov si, disk_error_msg
.print_char:
    lodsb
    test al, al
    jz .hang
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    jmp .print_char

.hang:
    cli
    hlt
    jmp .hang

boot_drive:     db 0
retry_count:    db 0

; Disk Address Packet for INT 13h extensions.
; LBA starts at 1 (sector right after the boot sector).
disk_address_packet:
    db 0x10                 ; size of DAP
    db 0x00                 ; reserved
    dw KERNEL_SECTORS       ; number of sectors to read
    dw LOAD_OFFSET          ; destination offset
    dw LOAD_SEGMENT         ; destination segment
    dq 1                    ; start LBA

disk_error_msg: db 'Disk Error', 0

times 510 - ($ - $$) db 0
dw 0xAA55
