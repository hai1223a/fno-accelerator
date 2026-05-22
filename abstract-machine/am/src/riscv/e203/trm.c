#include <am.h>
#include <riscv/e203/e203.h>

extern char _heap_start;
extern char _heap_end;
int main(const char *args);

Area heap = RANGE(&_heap_start, &_heap_end);
static const char mainargs[MAINARGS_MAX_LEN] = TOSTRING(MAINARGS_PLACEHOLDER); // defined in CFLAGS

typedef struct {
  union {
    volatile uint32_t rbr;
    volatile uint32_t dll;
    volatile uint32_t thr;
  };
  union {
    volatile uint32_t dlm;
    volatile uint32_t ier;
  };
  union {
    volatile uint32_t iir;
    volatile uint32_t fcr;
  };
  volatile uint32_t lcr;
  volatile uint32_t mcr;
  volatile uint32_t lsr;
} e203_uart_t;

static e203_uart_t *const uart0 = (e203_uart_t *)UART0_BASE;

static void uart0_init(void) {
  uart0->lcr = UART_LCR_DLAB;
  uart0->dll = 0;
  uart0->dlm = 0;
  uart0->fcr = UART_FCR_RST;
  uart0->lcr = UART_LCR_8N1;
}

void putch(char ch) {
  if (ch == '\n') {
    uart0->thr = '\r';
  }
  uart0->thr = (uint8_t)ch;
}

void halt(int code) {
  asm volatile("mv a0, %0; ebreak" : :"r"(code));
  while (1);
}
static void _id_puts() {
  uint32_t mvendorid, marchid;
  asm volatile ("csrr %0, mvendorid" : "=r"(mvendorid));
  asm volatile ("csrr %0, marchid" : "=r"(marchid));
  for (int i = 7; i >= 0; i--) {
      uint8_t nibble = (mvendorid >> (i * 4)) & 0xF;
      putch(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
  }
  putch('\n');
  for (int i = 7; i >= 0; i--) {
      uint8_t nibble = (marchid >> (i * 4)) & 0xF;
      putch(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
  }
  putch('\n');
}

void _trm_init() {
  uart0_init();
  _id_puts();
  int ret = main(mainargs);
  halt(ret);
}
