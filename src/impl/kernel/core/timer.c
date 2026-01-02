#include "include/timer.h"
#include "include/io.h"
#include "include/idt.h"
#include "include/stdio.h"
#include <stdint.h>

// Forward declaration for kprintf
extern void kprintf(const char *format, uint32_t color, ...);

// PIT (Programmable Interval Timer) ports
#define PIT_CHANNEL0 0x40
#define PIT_CHANNEL1 0x41
#define PIT_CHANNEL2 0x42
#define PIT_COMMAND  0x43

// PIT frequency
#define PIT_BASE_FREQ 1193182  // Hz

// Global state
static volatile uint64_t g_timer_ticks = 0;
static uint32_t g_timer_frequency = 0;

// PIT IRQ handler (IRQ0)
void timer_irq_handler(void) {
    g_timer_ticks++;
    
    // Send EOI (End of Interrupt) to PIC
    outb(0x20, 0x20);  // Master PIC EOI
}

void pit_init(uint32_t frequency_hz) {
    if (frequency_hz == 0 || frequency_hz > PIT_BASE_FREQ) {
        kprintf("TIMER: Invalid frequency %d Hz, using 100 Hz\n", 0xFFFF0000, frequency_hz);
        frequency_hz = 100;
    }

    g_timer_frequency = frequency_hz;
    
    // Calculate divisor for desired frequency
    uint32_t divisor = PIT_BASE_FREQ / frequency_hz;
    
    if (divisor > 65535) {
        divisor = 65535;  // Max 16-bit value
        g_timer_frequency = PIT_BASE_FREQ / divisor;
        kprintf("TIMER: Frequency clamped to %d Hz\n", 0xFFFF0000, g_timer_frequency);
    }

    kprintf("TIMER: Initializing PIT at %d Hz (divisor: %d)\n", 
            0x00FF0000, g_timer_frequency, divisor);

    // Configure PIT:
    // Channel 0, access mode lobyte/hibyte, mode 2 (rate generator), binary mode
    // Command byte: 00 11 010 0
    //   00    - Channel 0
    //   11    - Access mode: lobyte/hibyte
    //   010   - Mode 2: rate generator
    //   0     - Binary mode (not BCD)
    outb(PIT_COMMAND, 0x34);

    // Send divisor (low byte, then high byte)
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    kprintf("TIMER: PIT configured successfully\n", 0x00FF0000);
}

uint64_t timer_get_ticks(void) {
    return g_timer_ticks;
}

uint64_t timer_get_uptime_ms(void) {
    if (g_timer_frequency == 0) return 0;
    return (g_timer_ticks * 1000) / g_timer_frequency;
}

uint32_t timer_get_frequency(void) {
    return g_timer_frequency;
}

void timer_sleep_ms(uint32_t ms) {
    if (g_timer_frequency == 0) {
        // Fallback: busy wait
        for (volatile uint64_t i = 0; i < (uint64_t)ms * 100000; i++);
        return;
    }

    uint64_t target_ticks = g_timer_ticks + ((uint64_t)ms * g_timer_frequency / 1000);
    
    // Busy wait for now (will be replaced with scheduler sleep later)
    while (g_timer_ticks < target_ticks) {
        __asm__ volatile("hlt");  // Halt until next interrupt
    }
}
