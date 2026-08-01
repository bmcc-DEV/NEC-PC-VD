OUTPUT_FORMAT(elf32-littlemips)
OUTPUT_ARCH(mips)
ENTRY(_start)
SECTIONS {
  . = 0x1FC00000;
  .text : { *(.text) }
  .rodata : { *(.rodata) }
  .data : { *(.data) }
}
