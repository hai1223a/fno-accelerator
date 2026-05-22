.section .text.start, "ax"
.globl _start
.type _start, @function

_start:
  la    x5, trap_handler
  csrrw x0, mtvec, x5

  addi  x6, x0, 8
  csrrw x0, mstatus, x6
  csrrs x7, mstatus, x0
  csrrc x8, mstatus, x6
  csrrs x9, mstatus, x0
  csrrwi x10, mscratch, 3
  csrrsi x11, mscratch, 4
  csrrci x12, mscratch, 1

  ecall
after_ecall:
  addi  x31, x0, 1
  ebreak

trap_handler:
  csrrs x13, mepc, x0
  addi  x13, x13, 4
  csrrw x0, mepc, x13
  mret
