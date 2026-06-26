OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x8000;
    __kernel_start = .;

    /* Stage2 и критические данные (GDT, BootInfo, Таблицы страниц) 
       Всегда в первом сегменте, доступном для 16-бит. */
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

    . = 0x100000;

    .bss : ALIGN(4096)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    __kernel_end = .;
}
