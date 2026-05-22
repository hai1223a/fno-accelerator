ENTRY(_start)

/*
MEMORY {
  ilm (rxai!w) : ORIGIN = 0x80000000, LENGTH = 64K
  ram (wxa!ri) : ORIGIN = 0x90000000, LENGTH = 64K
}
*/

MEMORY {
  ilm (rxai!w) : ORIGIN = 0x80000000, LENGTH = 1M
  ram (wxa!ri) : ORIGIN = 0x90000000, LENGTH = 1M
}

REGION_ALIAS("IROM", ilm);
REGION_ALIAS("DRAM", ram);

/* 默认给 e203 预留 8KB 栈，其余 RAM 作为 data/bss/heap。*/
/* 0x10000 = 64KB 0x2000 = 8KB*/
_stack_size = 0x10000;

SECTIONS {
  .text : {
    *(entry)
    *(.text*)
  } > IROM AT> IROM
  etext = .;
  _etext = .;

  .rodata : {
    *(.rodata*)
  } > IROM AT> IROM
  rodata_load_start = LOADADDR(.rodata);

  . = ALIGN(8);
  .data : {
    _data = .;
    *(.data*)
    *(.sdata*)
    . = ALIGN(8);
  } > DRAM AT> IROM
  edata = .;
  data_load_start = LOADADDR(.data);

  .bss (NOLOAD) : {
    _bss_start = .;
    bss_start = .;
    *(.bss*)
    *(.sbss*)
    *(.scommon)
    . = ALIGN(8);
  } > DRAM

  end = .;
  _end = .;
  _heap_start = ALIGN(8);
  _heap_end = ORIGIN(DRAM) + LENGTH(DRAM) - _stack_size;

  .stack _heap_end (NOLOAD) : {
    . = ALIGN(16);
    _stack_top = .;
    . += _stack_size;
    _stack_pointer = .;
  } > DRAM
}
