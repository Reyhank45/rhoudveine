#include "include/stdio.h"
#include "include/ps2.h"
#include "include/console.h"
#include <stdarg.h>
#include <stdint.h>

int getchar(void) {
    return ps2_getchar();
}

int putchar(int c) {
    if (c == '\n') fb_putc('\n');
    else fb_putc((char)c);
    return c;
}

void puts(const char* s) {
    fb_puts(s);
}

// Simple sprintf implementation
static void int_to_str(char **buf, int64_t num, int is_signed) {
    char tmp[32];
    int i = 0;
    int neg = 0;
    uint64_t unum;
    
    if (is_signed && num < 0) {
        neg = 1;
        unum = (uint64_t)(-num);
    } else {
        unum = (uint64_t)num;
    }
    
    if (unum == 0) {
        tmp[i++] = '0';
    } else {
        while (unum > 0) {
            tmp[i++] = '0' + (unum % 10);
            unum /= 10;
        }
    }
    
    if (neg) *(*buf)++ = '-';
    while (i > 0) *(*buf)++ = tmp[--i];
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    char *start = buf;
    
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            int is_long = 0;
            if (*fmt == 'l') { is_long = 1; fmt++; }
            if (*fmt == 'u' && is_long) { is_long = 2; } // %lu
            
            switch (*fmt) {
                case 'd':
                case 'i':
                    if (is_long) {
                        int_to_str(&buf, va_arg(args, int64_t), 1);
                    } else {
                        int_to_str(&buf, va_arg(args, int), 1);
                    }
                    break;
                case 'u':
                    if (is_long >= 1) {
                        int_to_str(&buf, (int64_t)va_arg(args, uint64_t), 0);
                    } else {
                        int_to_str(&buf, (int64_t)va_arg(args, unsigned int), 0);
                    }
                    break;
                case 's': {
                    const char *s = va_arg(args, const char*);
                    while (*s) *buf++ = *s++;
                    break;
                }
                case '%':
                    *buf++ = '%';
                    break;
                default:
                    *buf++ = '%';
                    *buf++ = *fmt;
                    break;
            }
        } else {
            *buf++ = *fmt;
        }
        fmt++;
    }
    
    *buf = '\0';
    va_end(args);
    return (int)(buf - start);
}
