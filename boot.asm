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
%define MAX_SECTORS_PER_READ 127

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
    mov word [remaining_sectors], KERNEL_SECTORS
    mov word [dest_offset], LOAD_OFFSET
    mov word [dest_segment], LOAD_SEGMENT
    mov dword [current_lba], 1

.read_chunk:
    cmp word [remaining_sectors], 0
    je .payload_loaded

    mov ax, [remaining_sectors]
    cmp ax, MAX_SECTORS_PER_READ
    jbe .set_chunk
    mov ax, MAX_SECTORS_PER_READ

.set_chunk:
    mov [sectors_this_read], ax

    mov ax, [dest_segment]
    mov [disk_address_packet + 6], ax
    mov ax, [dest_offset]
    mov [disk_address_packet + 4], ax

    mov ax, [sectors_this_read]
    mov [disk_address_packet + 2], ax

    mov eax, [current_lba]
    mov [disk_address_packet + 8], eax
    mov dword [disk_address_packet + 12], 0

    ; INT 13h extensions (AH=42h) are used so large payloads load reliably
    ; regardless of disk geometry.
    mov si, disk_address_packet
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; Переходим к следующему куску загрузки:
    ; двигаем LBA, адрес назначения и остаток секторов.
    mov ax, [remaining_sectors]
    sub ax, [sectors_this_read]
    mov [remaining_sectors], ax

    mov eax, [current_lba]
    add eax, dword [sectors_this_read]
    mov [current_lba], eax

    mov ax, [sectors_this_read]
    mov bx, 512
    mul bx
    add word [dest_offset], ax
    jnc .read_chunk
    add word [dest_segment], 0x1000
    jmp .read_chunk

.payload_loaded:
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

boot_drive:        db 0
retry_count:       db 0
remaining_sectors: dw 0
sectors_this_read: dw 0
dest_offset:       dw 0
dest_segment:      dw 0
current_lba:       dd 0

; Disk Address Packet for INT 13h extensions.
; LBA starts at 1 (sector right after the boot sector).
disk_address_packet:
    db 0x10                 ; size of DAP
    db 0x00                 ; reserved
    dw 0                    ; number of sectors to read (runtime)
    dw 0                    ; destination offset (runtime)
    dw 0                    ; destination segment (runtime)
    dq 0                    ; start LBA (runtime)

disk_error_msg: db 'Disk Error', 0

times 510 - ($ - $$) db 0
dw 0xAA55
