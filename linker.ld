OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x8000;

    .text.boot : ALIGN(16)
    {
        KEEP(*(.text.boot))
    }

    .text : ALIGN(16)
    {
        *(.text .text.*)
    }

    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(16)
    {
        *(.data .data.*)
    }

    .bss : ALIGN(16)
    {
        *(.bss .bss.*)
        *(COMMON)
    }

    .paging : ALIGN(4096)
    {
        *(.paging)
    }

    . = ALIGN(512);
    .pad :
    {
        BYTE(0)
    }
}
