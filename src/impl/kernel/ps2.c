#include <stdint.h>
#include "ps2.h"
#include "console.h"
#include <stdint.h>

// Simple ring buffer for keyboard input
static volatile unsigned int in_head = 0;
static volatile unsigned int in_tail = 0;
static volatile char in_buf[256];

// Port addresses
#define KBD_DATA_PORT 0x60
#define KBD_STATUS_PORT 0x64

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}

// Minimal scancode set 1 -> ASCII map for common keys
static const char scancode_map[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b', '\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'', '`', 0,
    '\\','z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ',
};

void ps2_handle_interrupt(void) {
    uint8_t status = inb(KBD_STATUS_PORT);
    if (!(status & 1)) return; // no data
    uint8_t sc = inb(KBD_DATA_PORT);

    // handle only make codes (no key release: high bit set indicates break)
    if (sc & 0x80) return;

    if (sc < sizeof(scancode_map)) {
        char c = scancode_map[sc];
        if (c) {
            // enqueue
            unsigned int next = (in_head + 1) & 255;
            if (next != in_tail) {
                in_buf[in_head] = c;
                in_head = next;
            }
            // Do not echo here; let higher-level input routines handle echoing
        }
    }
}

int ps2_getchar(void) {
    // Wait for a character (enable interrupts around hlt)
    while (in_head == in_tail) {
        __asm__ volatile ("sti\n\thlt\n\tcli");
    }
    char c = in_buf[in_tail];
    in_tail = (in_tail + 1) & 255;
    return (int)c;
}
