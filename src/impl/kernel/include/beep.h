#ifndef BEEP_H
#define BEEP_H

// --------------------------------------------------------------------------
// 2. HARDWARE I/O & BEEP
// --------------------------------------------------------------------------
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

void beep(void) {
    // 1. Play 1000Hz Sound
    uint32_t divisor = 1193180 / 1000;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(divisor & 0xFF));
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));
    uint8_t tmp = inb(0x61);
    if (tmp != (tmp | 3)) outb(0x61, tmp | 3);

    // 2. Wait (using volatile to prevent optimizer deletion)
    for (volatile uint32_t i = 0; i < 10000000; i++) {
        __asm__ volatile("nop");
    }

    // 3. Stop Sound
    tmp = inb(0x61);
    outb(0x61, tmp & 0xFC);
}

#endif