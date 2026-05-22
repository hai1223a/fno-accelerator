.section .text.start, "ax"
.globl _start
.type _start, @function

_start:
  addi x1, x0, 5
  addi x2, x0, 5
  beq  x1, x2, 1f
  addi x3, x0, -1
  ebreak

1:
  jal  x0, 2f
  addi x3, x0, -2
  ebreak

2:
  addi x3, x0, 1
  ebreak

