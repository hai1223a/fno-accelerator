#ifndef KLIB_H__
#define KLIB_H__

#include <am.h>
/* 基础类型目前仍直接来源于 C 标准头：
 * - size_t 来自 <stddef.h>
 * - uint64_t / int32_t / int16_t 等固定宽度整数来自 <stdint.h>
 * 之所以保留这两个来源，是为了让上层业务代码只依赖 klib.h，
 * 而不必在各处重复显式包含标准头。 */
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#if defined(__ISA_NATIVE__) && !defined(__NATIVE_USE_KLIB__)
#include <time.h>
#endif
#include <klib-macros.h>

#ifdef __cplusplus
extern "C" {
#endif

// #define __NATIVE_USE_KLIB__

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)
// time.h (minimal)
typedef uint64_t time_t;
typedef uint64_t clock_t;
typedef int clockid_t;

struct timespec {
  time_t tv_sec;
  long tv_nsec;
};

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1
#define CLOCKS_PER_SEC  1000000ULL
#endif

// string.h
void  *memset    (void *s, int c, size_t n);
void  *memcpy    (void *dst, const void *src, size_t n);
void  *memmove   (void *dst, const void *src, size_t n);
int    memcmp    (const void *s1, const void *s2, size_t n);
size_t strlen    (const char *s);
char  *strcat    (char *dst, const char *src);
char  *strcpy    (char *dst, const char *src);
char  *strncpy   (char *dst, const char *src, size_t n);
int    strcmp    (const char *s1, const char *s2);
int    strncmp   (const char *s1, const char *s2, size_t n);

// stdlib.h
void   srand     (unsigned int seed);
int    rand      (void);
void  *malloc    (size_t size);
void   free      (void *ptr);
int    abs       (int x);
int    atoi      (const char *nptr);

// stdio.h
int    printf    (const char *format, ...);
int    sprintf   (char *str, const char *format, ...);
int    snprintf  (char *str, size_t size, const char *format, ...);
int    vsprintf  (char *str, const char *format, va_list ap);
int    vsnprintf (char *str, size_t size, const char *format, va_list ap);

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)
// time.h
int    clock_gettime (clockid_t clk_id, struct timespec *tp);
clock_t clock         (void);
time_t time           (time_t *tloc);
double difftime       (time_t end, time_t beginning);
#endif

// assert.h
#ifdef NDEBUG
  #define assert(ignore) ((void)0)
#else
  #define assert(cond) panic_on(!(cond), "Assertion `" TOSTRING(cond) "' failed")
#endif

#ifdef __cplusplus
}
#endif

#endif
