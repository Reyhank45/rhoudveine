#include "serial.h"
#include "include/io.h"
#include <stddef.h>

// COM1 base port
#define SERIAL_PORT 0x3F8

void serial_init(void) {
    // Disable interrupts
    outb(SERIAL_PORT + 1, 0x00);
    // Enable DLAB (set baud rate divisor)
    outb(SERIAL_PORT + 3, 0x80);
    // Set divisor to 1 (115200 baud)
    outb(SERIAL_PORT + 0, 0x01);
    outb(SERIAL_PORT + 1, 0x00);
    // 8 bits, no parity, one stop bit
    outb(SERIAL_PORT + 3, 0x03);
    // Enable FIFO, clear them, with 14-byte threshold
    outb(SERIAL_PORT + 2, 0xC7);
    // IRQs enabled, RTS/DSR set
    outb(SERIAL_PORT + 4, 0x0B);
}

static inline int serial_is_transmit_empty(void) {
    return inb(SERIAL_PORT + 5) & 0x20;
}

void serial_putc(char c) {
    // Wait for transmitter holding register to be empty
    while (!serial_is_transmit_empty()) { /* spin */ }
    outb(SERIAL_PORT + 0, (uint8_t)c);
}

void serial_write(const char *s) {
    for (size_t i = 0; s[i] != '\0'; i++) {
        char c = s[i];
        if (c == '\n') {
            serial_putc('\r');
            serial_putc('\n');
        } else {
            serial_putc(c);
        }
    }
}
