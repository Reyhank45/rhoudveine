#ifndef BEEP_H
#define BEEP_H

#include "io.h"

// The PIT (Programmable Interval Timer) runs at 1.193182 MHz
#define PIT_FREQUENCY 1193180

// Play sound at a specific frequency
void play_sound(uint32_t frequency) {
    uint32_t divisor = PIT_FREQUENCY / frequency;

    // 1. Configure the PIT (Port 0x43)
    // 0xB6 = Binary mode, Mode 3 (Square Wave), Access lo/hi byte, Channel 2
    outb(0x43, 0xB6);

    // 2. Send the frequency divisor (Port 0x42)
    // Send lower byte first, then upper byte
    outb(0x42, (uint8_t)(divisor & 0xFF));
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));

    // 3. Enable the Speaker (Port 0x61)
    // Read current state
    uint8_t tmp = inb(0x61);
    // Check if it's already on. If not, turn on bits 0 and 1.
    if (tmp != (tmp | 3)) {
        outb(0x61, tmp | 3);
    }
}

// Stop the sound
void stop_sound() {
    // Read current state from Port 0x61
    uint8_t tmp = inb(0x61);
    // Clear bits 0 and 1 to turn it off
    outb(0x61, tmp & 0xFC);
}

// Simple busy-wait delay (Not accurate, but works for a beep)
void delay(uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        io_wait(); // Consumes a few CPU cycles
    }
}

// The Function you want: BEEP!
void beep(uint32_t freq, uint32_t duration) {
    play_sound(freq); // 1000 Hz (Standard Beep)
    delay(duration);  // Wait roughly 100ms
    stop_sound();
}

#endif