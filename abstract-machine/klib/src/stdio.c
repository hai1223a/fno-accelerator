#include <am.h>
#include <klib.h>
#include <klib-macros.h>
#include <stdarg.h>
#include <limits.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

static int parse_length_modifier(const char **fmt) {
  int length = 0;
  while (**fmt == 'l') {
    length++;
    (*fmt)++;
  }
  return length;
}

static int format_unsigned_rev_ull(unsigned long long num, unsigned int base,
                                   int uppercase, char *buf, int buf_size) {
  int n = 0;

  if (buf_size <= 0) {
    return 0;
  }

  do {
    unsigned int digit = (unsigned int)(num % base);
    if (digit < 10) {
      buf[n++] = (char)('0' + digit);
    } else {
      buf[n++] = (char)((uppercase ? 'A' : 'a') + (digit - 10));
    }
    num /= base;
  } while (num > 0 && n < buf_size);

  return n;
}

int printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int count = 0;
  while (*fmt) {
    if (*fmt == '%') {
      fmt++;
      // 处理零填充和宽度
      int zero_pad = 0, width = 0;
      if (*fmt == '0') {
        zero_pad = 1;
        fmt++;
      }
      while (*fmt >= '0' && *fmt <= '9') {
        width = width * 10 + (*fmt - '0');
        fmt++;
      }
      int length = parse_length_modifier(&fmt);
      // 这里识别各种通配符
      switch (*fmt) {
        case 'd': {
          long long num = (length >= 2) ? va_arg(ap, long long) : va_arg(ap, int);
          char num_str[32];
          int n = 0, neg = 0;
          unsigned long long unum;
          if (num < 0) {
            neg = 1;
            unum = (unsigned long long)(-(num + 1)) + 1;
          } else {
            unum = (unsigned long long)num;
          }
          do {
            num_str[n++] = (unum % 10) + '0';
            unum /= 10;
          } while (unum > 0);
          if (neg) num_str[n++] = '-';
          int pad = width - n;
          if (zero_pad && pad > 0) {
            if (neg) {
              putch('-');
              count++;
              n--;
            }
            for (int i = 0; i < pad; i++) {
              putch('0');
              count++;
            }
            for (int i = n - 1; i >= (neg ? 1 : 0); i--) {
              putch(num_str[i]);
              count++;
            }
          } else {
            for (int i = 0; i < pad; i++) {
              putch(' ');
              count++;
            }
            for (int i = n - 1; i >= 0; i--) {
              putch(num_str[i]);
              count++;
            }
          }
          break;
        }
        case 'u': {
          unsigned long long num = (length >= 2) ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int);
          char num_str[32];
          int n = format_unsigned_rev_ull(num, 10, 0, num_str, (int)sizeof(num_str));
          int pad = width - n;
          for (int i = 0; i < pad; i++) {
            putch(zero_pad ? '0' : ' ');
            count++;
          }
          for (int i = n - 1; i >= 0; i--) {
            putch(num_str[i]);
            count++;
          }
          break;
        }
        case 'x':
        case 'X': {
          unsigned long long num = (length >= 2) ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int);
          char num_str[32];
          int n = format_unsigned_rev_ull(num, 16, *fmt == 'X', num_str, (int)sizeof(num_str));
          int pad = width - n;
          for (int i = 0; i < pad; i++) {
            putch(zero_pad ? '0' : ' ');
            count++;
          }
          for (int i = n - 1; i >= 0; i--) {
            putch(num_str[i]);
            count++;
          }
          break;
        }
        case 'o': {
          unsigned long long num = (length >= 2) ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int);
          char num_str[32];
          int n = format_unsigned_rev_ull(num, 8, 0, num_str, (int)sizeof(num_str));
          int pad = width - n;
          for (int i = 0; i < pad; i++) {
            putch(zero_pad ? '0' : ' ');
            count++;
          }
          for (int i = n - 1; i >= 0; i--) {
            putch(num_str[i]);
            count++;
          }
          break;
        }
        case 'p': {
          void *ptr = va_arg(ap, void *);
          uintptr_t addr = (uintptr_t)ptr;
          putch('0'); putch('x'); count += 2;
          char num_str[2 * sizeof(uintptr_t) + 1];
          int n = 0;
          if (addr == 0) {
            putch('0'); count++;
          } else {
            while (addr) {
              int digit = addr % 16;
              num_str[n++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
              addr /= 16;
            }
            for (int i = n - 1; i >= 0; i--) {
              putch(num_str[i]);
              count++;
            }
          }
          break;
        }
        case 's': {
          const char *s = va_arg(ap, const char *);
          int slen = 0;
          const char *sp = s;
          while (*sp++) slen++;
          int pad = width - slen;
          for (int i = 0; i < pad; i++) {
            putch(' ');
            count++;
          }
          while (*s) {
            putch(*(s++));
            count++;
          }
          break;
        }
        case 'c': {
          char ch = (char)va_arg(ap, int);
          putch(ch);
          count++;
          break;
        }
        case '%':
          putch('%');
          count++;
          break;
        default:
          putch('%');
          count++;
          putch(*fmt);
          count++;
          break;
      }
    } else {
      putch(*fmt);
      count++;
    }
    fmt++;
  }
  va_end(ap);
  return count;
}

int vsprintf(char *out, const char *fmt, va_list ap) {
  char *head = out;
  while (*fmt) {
    if (*fmt == '%') {
      fmt++;
      // 处理零填充和宽度
      int zero_pad = 0, width = 0;
      if (*fmt == '0') {
        zero_pad = 1;
        fmt++;
      }
      while (*fmt >= '0' && *fmt <= '9') {
        width = width * 10 + (*fmt - '0');
        fmt++;
      }
      int length = parse_length_modifier(&fmt);
      switch (*fmt) {
        case 'd': {
          long long num = (length >= 2) ? va_arg(ap, long long) : va_arg(ap, int);
          char num_str[32];
          int n = 0, neg = 0;
          unsigned long long unum;
          if (num < 0) {
            neg = 1;
            unum = (unsigned long long)(-(num + 1)) + 1;
          } else {
            unum = (unsigned long long)num;
          }
          do {
            num_str[n++] = (unum % 10) + '0';
            unum /= 10;
          } while (unum > 0);
          if (neg) num_str[n++] = '-';
          int pad = width - n;
          if (zero_pad && pad > 0) {
            if (neg) {
              *(out++) = '-';
              n--;
            }
            for (int i = 0; i < pad; i++) {
              *(out++) = '0';
            }
            for (int i = n - 1; i >= (neg ? 1 : 0); i--) {
              *(out++) = num_str[i];
            }
          } else {
            for (int i = 0; i < pad; i++) {
              *(out++) = ' ';
            }
            for (int i = n - 1; i >= 0; i--) {
              *(out++) = num_str[i];
            }
          }
          break;
        }
        case 'u': {
          unsigned long long num = (length >= 2) ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int);
          char num_str[32];
          int n = format_unsigned_rev_ull(num, 10, 0, num_str, (int)sizeof(num_str));
          int pad = width - n;
          for (int i = 0; i < pad; i++) {
            *(out++) = zero_pad ? '0' : ' ';
          }
          for (int i = n - 1; i >= 0; i--) {
            *(out++) = num_str[i];
          }
          break;
        }
        case 'x':
        case 'X': {
          unsigned long long num = (length >= 2) ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int);
          char num_str[32];
          int n = format_unsigned_rev_ull(num, 16, *fmt == 'X', num_str, (int)sizeof(num_str));
          int pad = width - n;
          for (int i = 0; i < pad; i++) {
            *(out++) = zero_pad ? '0' : ' ';
          }
          for (int i = n - 1; i >= 0; i--) {
            *(out++) = num_str[i];
          }
          break;
        }
        case 'o': {
          unsigned long long num = (length >= 2) ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int);
          char num_str[32];
          int n = format_unsigned_rev_ull(num, 8, 0, num_str, (int)sizeof(num_str));
          int pad = width - n;
          for (int i = 0; i < pad; i++) {
            *(out++) = zero_pad ? '0' : ' ';
          }
          for (int i = n - 1; i >= 0; i--) {
            *(out++) = num_str[i];
          }
          break;
        }
        case 'p': {
          void *ptr = va_arg(ap, void *);
          uintptr_t addr = (uintptr_t)ptr;
          *(out++) = '0'; *(out++) = 'x';
          char num_str[2 * sizeof(uintptr_t) + 1];
          int n = 0;
          if (addr == 0) {
            *(out++) = '0';
          } else {
            while (addr) {
              int digit = addr % 16;
              num_str[n++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
              addr /= 16;
            }
            for (int i = n - 1; i >= 0; i--) {
              *(out++) = num_str[i];
            }
          }
          break;
        }
        case 's': {
          const char *s = va_arg(ap, const char *);
          int slen = 0;
          const char *sp = s;
          while (*sp++) slen++;
          int pad = width - slen;
          for (int i = 0; i < pad; i++) {
            *(out++) = ' ';
          }
          while (*s) {
            *(out++) = *(s++);
          }
          break;
        }
        case 'c': {
          char ch = (char)va_arg(ap, int);
          *(out++) = ch;
          break;
        }
        case '%':
          *(out++) = '%';
          break;
        default:
          *(out++) = '%';
          *(out++) = *fmt;
          break;
      }
    } else {
      *(out++) = *fmt;
    }
    fmt++;
  }
  *out = '\0';
  return out - head;
}

int sprintf(char *out, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int written = vsprintf(out, fmt, ap);
  va_end(ap);
  return written;
}

int vsnprintf(char *out, size_t n, const char *fmt, va_list ap) {
  size_t remain = n;
  int total = 0; // 记录本应写入的字符数

  while (*fmt) {
    if (*fmt == '%') {
      fmt++;
      int zero_pad = 0, width = 0;
      if (*fmt == '0') {
        zero_pad = 1;
        fmt++;
      }
      while (*fmt >= '0' && *fmt <= '9') {
        width = width * 10 + (*fmt - '0');
        fmt++;
      }
      int length = parse_length_modifier(&fmt);
      switch (*fmt) {
        case 'd': {
          long long num = (length >= 2) ? va_arg(ap, long long) : va_arg(ap, int);
          char num_str[32];
          int nstr = 0, neg = 0;
          unsigned long long unum;
          if (num < 0) { neg = 1; unum = (unsigned long long)(-(num + 1)) + 1; }
          else { unum = (unsigned long long)num; }
          do { num_str[nstr++] = (unum % 10) + '0'; unum /= 10; } while (unum > 0);
          if (neg) num_str[nstr++] = '-';
          int pad = width - nstr;
          if (zero_pad && pad > 0) {
            if (neg) { if (remain > 1) *(out++) = '-'; nstr--; if (remain > 1) remain--; total++; }
            for (int i = 0; i < pad; i++, total++) {
              if (remain > 1) { *(out++) = '0'; remain--; }
            }
            for (int i = nstr - 1; i >= (neg ? 1 : 0); i--, total++) {
              if (remain > 1) { *(out++) = num_str[i]; remain--; }
            }
          } else {
            for (int i = 0; i < pad; i++, total++) {
              if (remain > 1) { *(out++) = ' '; remain--; }
            }
            for (int i = nstr - 1; i >= 0; i--, total++) {
              if (remain > 1) { *(out++) = num_str[i]; remain--; }
            }
          }
          break;
        }
        case 'u': {
          unsigned long long num = (length >= 2) ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int);
          char num_str[32];
          int nstr = format_unsigned_rev_ull(num, 10, 0, num_str, (int)sizeof(num_str));
          int pad = width - nstr;
          for (int i = 0; i < pad; i++, total++) {
            if (remain > 1) { *(out++) = zero_pad ? '0' : ' '; remain--; }
          }
          for (int i = nstr - 1; i >= 0; i--, total++) {
            if (remain > 1) { *(out++) = num_str[i]; remain--; }
          }
          break;
        }
        case 'x':
        case 'X': {
          unsigned long long num = (length >= 2) ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int);
          char num_str[32];
          int nstr = format_unsigned_rev_ull(num, 16, *fmt == 'X', num_str, (int)sizeof(num_str));
          int pad = width - nstr;
          for (int i = 0; i < pad; i++, total++) {
            if (remain > 1) { *(out++) = zero_pad ? '0' : ' '; remain--; }
          }
          for (int i = nstr - 1; i >= 0; i--, total++) {
            if (remain > 1) { *(out++) = num_str[i]; remain--; }
          }
          break;
        }
        case 'o': {
          unsigned long long num = (length >= 2) ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int);
          char num_str[32];
          int nstr = format_unsigned_rev_ull(num, 8, 0, num_str, (int)sizeof(num_str));
          int pad = width - nstr;
          for (int i = 0; i < pad; i++, total++) {
            if (remain > 1) { *(out++) = zero_pad ? '0' : ' '; remain--; }
          }
          for (int i = nstr - 1; i >= 0; i--, total++) {
            if (remain > 1) { *(out++) = num_str[i]; remain--; }
          }
          break;
        }
        case 'p': {
          void *ptr = va_arg(ap, void *);
          uintptr_t addr = (uintptr_t)ptr;
          if (remain > 1) { *(out++) = '0'; remain--; }
          total++;
          if (remain > 1) { *(out++) = 'x'; remain--; }
          total++;
          char num_str[2 * sizeof(uintptr_t) + 1];
          int nstr = 0;
          if (addr == 0) {
            if (remain > 1) { *(out++) = '0'; remain--; }
            total++;
          } else {
            while (addr) {
              int digit = addr % 16;
              num_str[nstr++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
              addr /= 16;
            }
            for (int i = nstr - 1; i >= 0; i--, total++) {
              if (remain > 1) { *(out++) = num_str[i]; remain--; }
            }
          }
          break;
        }
        case 's': {
          const char *s = va_arg(ap, const char *);
          int slen = 0; const char *sp = s;
          while (*sp++) slen++;
          int pad = width - slen;
          for (int i = 0; i < pad; i++, total++) {
            if (remain > 1) { *(out++) = ' '; remain--; }
          }
          while (*s) {
            if (remain > 1) { *(out++) = *s; remain--; }
            s++; total++;
          }
          break;
        }
        case 'c': {
          char ch = (char)va_arg(ap, int);
          if (remain > 1) { *(out++) = ch; remain--; }
          total++;
          break;
        }
        case '%':
          if (remain > 1) { *(out++) = '%'; remain--; }
          total++;
          break;
        default:
          if (remain > 1) { *(out++) = '%'; remain--; }
          if (remain > 1) { *(out++) = *fmt; remain--; }
          total += 2;
          break;
      }
    } else {
      if (remain > 1) { *(out++) = *fmt; remain--; }
      total++;
    }
    fmt++;
  }
  *out = '\0';
  return total;
}

int snprintf(char *out, size_t n, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int written = vsnprintf(out, n, fmt, ap);
  va_end(ap);
  return written;
}

#endif
