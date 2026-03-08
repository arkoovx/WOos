OUTPUT_FORMAT(binary)
ENTRY(_start)

SECTIONS
{
    . = 0x7C00;

    .boot_sector : ALIGN(16)
    {
        KEEP(*(.boot_sector))
    }

    . = 0x8000;

    .text : ALIGN(16)
    {
        KEEP(*(.stage2_entry))
        *(.stage2)
        *(.text*)
        *(.rodata*)
        *(.data*)
        *(.bss*)
        *(COMMON)
    }

    .paging : ALIGN(4096)
    {
        *(.paging)
    }
}
