; WoOS boot sector (stage 1)
; Loads stage2+kernel payload to 0000:8000 and jumps there.

BITS 16
ORG 0x7C00

%define STAGE2_SECTORS 64

boot_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [boot_drive], dl

    mov si, disk_address_packet
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc boot_fail

    jmp 0x0000:0x8000

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
    dq 1

TIMES 510 - ($ - $$) db 0
DW 0xAA55
