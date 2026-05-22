#ifndef NPC_H__
#define NPC_H__

#include <klib-macros.h>
#include <riscv/riscv.h>

#define UART0_BASE      0x10013000u
#define UART_LCR_DLAB (1u << 7)
#define UART_LCR_8N1  0x03u
#define UART_FCR_RST  0x06u

#define RTC_ADDR        0x02000000

#endif