.section .text.start, "ax"
.globl _start
.type _start, @function

_start:
  addi x1, x0, 21
  addi x2, x0, 21
  add  x3, x1, x2

  lui  x4, 0x90000
  sw   x3, 0(x4)
  lw   x5, 0(x4)

  ebreak

