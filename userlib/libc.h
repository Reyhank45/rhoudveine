#ifndef _LIBC_H
#define _LIBC_H

#include "syscall.h"

// Standard file descriptors
#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

// NULL pointer
#ifndef NULL
#define NULL ((void *)0)
#endif

// Boolean types
#ifndef true
#define true 1
#define false 0
#endif

// --- String functions ---

static inline size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static inline char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++) != '\0');
    return dst;
}

static inline int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

static inline void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n-- > 0) *p++ = (unsigned char)c;
    return s;
}

static inline void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n-- > 0) *d++ = *s++;
    return dst;
}

// --- I/O functions ---

static inline int putchar(int c) {
    char ch = (char)c;
    write(STDOUT_FILENO, &ch, 1);
    return c;
}

static inline int puts(const char *s) {
    write(STDOUT_FILENO, s, strlen(s));
    putchar('\n');
    return 0;
}

static inline int getchar(void) {
    char c;
    if (read(STDIN_FILENO, &c, 1) <= 0) return -1;
    return (int)c;
}

// Simple printf (supports %s, %d, %x, %c, %%)
static inline void print_int(long n) {
    if (n < 0) {
        putchar('-');
        n = -n;
    }
    if (n >= 10) print_int(n / 10);
    putchar('0' + (n % 10));
}

static inline void print_hex(unsigned long n) {
    if (n >= 16) print_hex(n / 16);
    int d = n % 16;
    putchar(d < 10 ? '0' + d : 'a' + d - 10);
}

static inline int printf(const char *fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    
    int count = 0;
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 's': {
                    const char *s = __builtin_va_arg(args, const char *);
                    if (!s) s = "(null)";
                    while (*s) { putchar(*s++); count++; }
                    break;
                }
                case 'd':
                case 'i': {
                    long n = __builtin_va_arg(args, long);
                    print_int(n);
                    count++;
                    break;
                }
                case 'x': {
                    unsigned long n = __builtin_va_arg(args, unsigned long);
                    print_hex(n);
                    count++;
                    break;
                }
                case 'c': {
                    int c = __builtin_va_arg(args, int);
                    putchar(c);
                    count++;
                    break;
                }
                case '%':
                    putchar('%');
                    count++;
                    break;
                default:
                    putchar('%');
                    putchar(*fmt);
                    count += 2;
                    break;
            }
        } else {
            putchar(*fmt);
            count++;
        }
        fmt++;
    }
    
    __builtin_va_end(args);
    return count;
}

// --- Memory allocation (simple bump allocator) ---

static char _heap[65536];  // 64KB heap
static size_t _heap_pos = 0;

static inline void *malloc(size_t size) {
    // Align to 8 bytes
    size = (size + 7) & ~7;
    if (_heap_pos + size > sizeof(_heap)) return NULL;
    void *ptr = &_heap[_heap_pos];
    _heap_pos += size;
    return ptr;
}

static inline void free(void *ptr) {
    (void)ptr;  // Simple bump allocator doesn't free
}

// --- Entry point macro ---

// User program should define main()
extern int main(int argc, char **argv);

// Startup code (called by kernel)
static inline void _start(void) {
    // Call main with no arguments for now
    int ret = main(0, NULL);
    exit(ret);
}

#endif // _LIBC_H
