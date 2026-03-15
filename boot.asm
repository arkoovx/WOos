; WoOS boot sector (stage 1, real mode only)
; Reads stage2+kernel payload from disk to 0000:8000 and jumps there.

BITS 16
ORG 0x7C00

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 64
%endif

%define LOAD_SEGMENT 0x0800      ; 0x0800:0x0000 == физический адрес 0x8000
%define LOAD_OFFSET  0x0000
%define RESET_RETRIES 5
%define MAX_DAP_SECTORS 127

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
    ; Важный нюанс: ряд BIOS/эмуляторов плохо работает, если один вызов AH=42h
    ; пишет буфер через границу 64 KiB. Поэтому читаем payload порциями,
    ; каждая из которых гарантированно помещается в один 64 KiB-сегмент.
    mov word [dap_sector_count], KERNEL_SECTORS
    mov word [dap_dest_segment], LOAD_SEGMENT
    mov dword [dap_lba_low], 1
    mov dword [dap_lba_high], 0

.read_payload_loop:
    cmp word [dap_sector_count], 0
    je .boot_kernel

    mov ax, [dap_sector_count]
    cmp ax, MAX_DAP_SECTORS
    jbe .chunk_ready
    mov ax, MAX_DAP_SECTORS
.chunk_ready:
    mov [dap_chunk_count], ax

    ; INT 13h extensions (AH=42h) — чтение из LBA в заданный буфер.
    mov si, disk_address_packet
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; remaining -= chunk
    sub word [dap_sector_count], ax

    ; segment += chunk * 32 paragraphs (512 байт = 32 параграфа)
    mov bx, ax
    shl bx, 5
    add word [dap_dest_segment], bx

    ; lba += chunk
    movzx eax, ax
    add dword [dap_lba_low], eax
    adc dword [dap_lba_high], 0

    jmp .read_payload_loop

.boot_kernel:
    jmp 0x0000:0x8000

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
dap_chunk_count:
    dw 0                    ; chunk sectors (заполняется перед int13h)
    dw LOAD_OFFSET          ; destination offset
dap_dest_segment:
    dw LOAD_SEGMENT         ; destination segment (обновляется по мере чтения)
dap_lba_low:
    dd 1                    ; start LBA (low dword)
dap_lba_high:
    dd 0                    ; start LBA (high dword)

; Переменная цикла чтения payload.
dap_sector_count: dw 0

disk_error_msg: db 'Disk Error', 0

times 510 - ($ - $$) db 0
dw 0xAA55
