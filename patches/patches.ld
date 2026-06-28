/* Minimal linker script for the recomp patches.
 * Adapt from Zelda64Recompiled/patches/patches.ld once real patches exist.
 * The load address here is illustrative; the recompiler relocates patch symbols. */
SECTIONS {
    .text 0x80000000 : { *(.text) *(.text.*) }
    .data            : { *(.data) *(.data.*) }
    .rodata          : { *(.rodata) *(.rodata.*) }
    .bss             : { *(.bss) *(.bss.*) *(COMMON) }

    /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.comment) *(.gnu.attributes) }
}
