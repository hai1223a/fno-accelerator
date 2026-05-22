#include <am.h>
#include <klib.h>
#include <klib-macros.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

static uint64_t klib_uptime_us(void) {
  return io_read(AM_TIMER_UPTIME).us;
}

clock_t clock(void) {
  return (clock_t)klib_uptime_us();
}

time_t time(time_t *tloc) {
  time_t now = (time_t)(klib_uptime_us() / 1000000ULL);
  if (tloc != NULL) {
    *tloc = now;
  }
  return now;
}

double difftime(time_t end, time_t beginning) {
  return (double)(end - beginning);
}

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
  uint64_t now_us;

  if (tp == NULL) {
    return -1;
  }
  if (clk_id != CLOCK_REALTIME && clk_id != CLOCK_MONOTONIC) {
    return -1;
  }

  now_us = klib_uptime_us();
  tp->tv_sec = (time_t)(now_us / 1000000ULL);
  tp->tv_nsec = (long)((now_us % 1000000ULL) * 1000ULL);
  return 0;
}

#endif
