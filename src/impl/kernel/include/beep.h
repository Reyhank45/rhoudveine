#ifndef BEEP_H
#define BEEP_H

#include "io.h"

#define PIT_FREQUENCY 1193180

void play_sound(uint32_t frequency) {
    uint32_t divisor = PIT_FREQUENCY / frequency;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(divisor & 0xFF));
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));

    uint8_t tmp = inb(0x61);
    if (tmp != (tmp | 3)) {
        outb(0x61, tmp | 3);
    }
}

void stop_sound() {
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp & 0xFC);
}

// FIXED DELAY: Uses volatile to prevent the compiler from deleting the loop
void delay(uint32_t count) {
    for (volatile uint32_t i = 0; i < count; i++) {
        __asm__ volatile("nop"); // "No Operation" - burns 1 CPU cycle
    }
}

void beep(uint32_t frequency, uint32_t duration) {
    play_sound(frequency);
    // Increased count because modern CPUs are extremely fast
    // 10 million cycles is nothing on a 3GHz CPU.
    // Let's try 100 million.
    delay(duration); 
    stop_sound();
}

#endif