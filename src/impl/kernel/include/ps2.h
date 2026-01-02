#pragma once

// Called by IRQ handler when keyboard data is available
void ps2_handle_interrupt(void);

// Blocking getchar from PS/2 input (returns ASCII code)
int ps2_getchar(void);
// Non-blocking getchar: returns -1 when no character available
int try_getchar(void);

void ps2_init(void);
