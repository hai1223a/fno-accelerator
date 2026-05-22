.section .text.start, "ax"
.globl _start
.type _start, @function

_start:
  addi x1, x0, 1
  fence.i
  addi x2, x1, 2
  ebreak
