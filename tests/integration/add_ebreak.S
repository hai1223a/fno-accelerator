.section .text.start, "ax"
.globl _start
.type _start, @function

_start:
  addi x1, x0, 7
  addi x2, x0, 35
  add  x3, x1, x2
  add  x4, x1, x2
  add  x5, x1, x2
  add  x6, x1, x2
  add  x7, x1, x2
  add  x8, x1, x2
  add  x9, x1, x2
  add  x10, x1, x2
  ebreak

