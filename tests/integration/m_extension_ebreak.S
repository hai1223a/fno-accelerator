    .section .text
    .globl _start
_start:
    addi x1, x0, 7
    addi x2, x0, 6
    mul  x3, x1, x2
    addi x4, x0, -1
    mulh x5, x4, x2
    div  x6, x3, x1
    rem  x7, x3, x1
    divu x8, x3, x0
    remu x9, x3, x0
    ebreak
