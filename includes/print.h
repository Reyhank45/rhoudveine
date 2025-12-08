#ifndef INCLUDES_PRINT_H
#define INCLUDES_PRINT_H

#include <stdarg.h>
#include <stdint.h>

// Simple printing API for early-init and init systems.
// This header provides printf-style functions and log helpers.

// Initialize the print subsystem (selects backend). Call early from kernel.
void init_print(void);

// Basic output primitives
void printc(char c);
void prints(const char *s);

// Minimal printf (supports %s, %d, %u, %x, %c)
void printf(const char *fmt, ...);
void vprintf(const char *fmt, va_list ap);

// Log helpers (prefix messages with level tags)
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);

// Convenience macros for init scripts
#define INFO(...) log_info(__VA_ARGS__)
#define WARN(...) log_warn(__VA_ARGS__)
#define ERROR(...) log_error(__VA_ARGS__)

#endif // INCLUDES_PRINT_H
