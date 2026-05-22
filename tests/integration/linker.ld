ENTRY(_start)

MEMORY {
  itcm (rxai!w) : ORIGIN = 0x80000000, LENGTH = 2M
}

SECTIONS {
  . = ORIGIN(itcm);

  .text : {
    KEEP(*(.text.start))
    *(.text*)
  } > itcm

  .rodata : {
    *(.rodata*)
  } > itcm

  .data : {
    *(.data*)
    *(.sdata*)
  } > itcm

  .bss (NOLOAD) : {
    *(.bss*)
    *(.sbss*)
    *(COMMON)
  } > itcm
}

