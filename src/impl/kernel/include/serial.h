#pragma once

#include <stdint.h>

// Initialize COM1 (0x3F8) serial port for 115200 8N1
void serial_init(void);

// Write a single character to serial
void serial_putc(char c);

// Write a NUL-terminated string to serial
void serial_write(const char *s);
