; WoOS boot sector (stage 1)
; Loads stage2+kernel payload to 0000:8000 and jumps there.

BITS 16
ORG 0x7C00

%define STAGE2_SECTORS 64
%define MAX_RETRIES    3

boot_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [boot_drive], dl

    mov si, disk_address_packet
    mov byte [retry_count], MAX_RETRIES

.read_retry:
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jnc .check_count

    mov ah, 0x00
    int 0x13

    dec byte [retry_count]
    jnz .read_retry
    jmp boot_fail

.check_count:
    cmp word [disk_address_packet + 2], STAGE2_SECTORS
    jne boot_fail

    jmp 0x0000:0x8000

boot_fail:
    hlt
    jmp boot_fail

boot_drive: db 0
retry_count: db 0

disk_address_packet:
    db 0x10
    db 0x00
    dw STAGE2_SECTORS
    dw 0x8000
    dw 0x0000
    dq 1

TIMES 510 - ($ - $$) db 0
DW 0xAA55
