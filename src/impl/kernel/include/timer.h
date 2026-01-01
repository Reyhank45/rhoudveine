#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

#include <stdint.h>

// Initialize the Programmable Interval Timer (PIT)
void pit_init(uint32_t frequency_hz);

// Get system uptime in milliseconds
uint64_t timer_get_uptime_ms(void);

// Get system uptime in ticks
uint64_t timer_get_ticks(void);

// Sleep for specified milliseconds (busy-wait for now, will use scheduler later)
void timer_sleep_ms(uint32_t ms);

// Get ticks per second
uint32_t timer_get_frequency(void);

#endif // KERNEL_TIMER_H
