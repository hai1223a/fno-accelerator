.section entry, "ax"
.globl _start
.type _start, @function

_start:
  # Keep all interrupts disabled during the whole boot flow.
  csrci mstatus, 8
  csrw mie, zero

  # Use the AM trap entry, and force direct mode.
  la t0, __am_asm_trap
  andi t0, t0, -4
  csrw mtvec, t0

  # Set up the boot stack before calling into C code.
  la sp, _stack_pointer

  # Copy initialized data from ILM load address to RAM runtime address.
  la a0, data_load_start
  la a1, _data
  la a2, edata
  bgeu a1, a2, 2f
1:
  lw t0, 0(a0)
  sw t0, 0(a1)
  addi a0, a0, 4
  addi a1, a1, 4
  bltu a1, a2, 1b

  # Clear the BSS range in RAM.
2:
  la a0, bss_start
  la a1, _end
  bgeu a0, a1, 4f
3:
  sw zero, 0(a0)
  addi a0, a0, 4
  bltu a0, a1, 3b

4:
  mv ra, zero
  mv s0, zero
  call _trm_init

5:
  j 5b
