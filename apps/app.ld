ENTRY(_start)
SECTIONS {
    . = 0x50000;
    .text : { *(.text) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
}
