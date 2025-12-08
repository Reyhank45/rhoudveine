#pragma once

// Called by IRQ handler when keyboard data is available
void ps2_handle_interrupt(void);

// Blocking getchar from PS/2 input (returns ASCII code)
int ps2_getchar(void);
