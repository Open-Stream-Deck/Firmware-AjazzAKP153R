ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx) : ORIGIN = 0x08004000, LENGTH = 0x178F0
    RAM (rw) : ORIGIN = 0x20000000, LENGTH = 0x20000
}

__stack_top = ORIGIN(RAM) + LENGTH(RAM);
__stack_bottom = __stack_top - 0x2000;

SECTIONS
{
    .vectors :
    {
        KEEP(*(.vectors))
    } > FLASH

    .text :
    {
        *(.text.Reset_Handler)
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    .data :
    {
        . = ALIGN(4);
        __data_start = .;
        *(.data*)
        . = ALIGN(4);
        __data_end = .;
    } > RAM AT> FLASH

    __data_load = LOADADDR(.data);

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM

    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        *(.noinit*)
        . = ALIGN(4);
        __noinit_end = .;
    } > RAM

    __image_end = LOADADDR(.data) + SIZEOF(.data);

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.ARM.exidx*)
        *(.ARM.extab*)
    }
}

ASSERT(SIZEOF(.vectors) == 320, "Expected 80 vector words")
ASSERT(ADDR(.vectors) == 0x08004000, "Application base mismatch")
ASSERT(__noinit_end <= __stack_bottom, "Application data overlaps reserved stack")
ASSERT(__image_end <= ORIGIN(FLASH) + LENGTH(FLASH), "Application exceeds flash region")
