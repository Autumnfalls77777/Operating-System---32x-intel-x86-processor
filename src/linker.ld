/* ============================================================================
 * src/linker.ld  -  Flat 32-bit kernel linked for physical address 0x10000
 * ============================================================================ */

ENTRY(start)

SECTIONS
{
    . = 0x10000;
    __kernel_start = .;

    .text : {
        KEEP(*(.text.entry))
        *(.text*)
    }

    .rodata : {
        *(.rdata*)
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    . = ALIGN(16);
    __bss_start = .;

    .bss : {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        . = ALIGN(16);
        __bss_end = .;
    }

    . = ALIGN(16);
    __kernel_end = .;
}
