#pragma once

#include <stdint.h>

// Called from assembly IRQ stubs with IRQ number (0..15)
void irq_handler(int irq);
