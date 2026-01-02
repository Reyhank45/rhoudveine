#pragma once

#include <stdint.h>

// Put a single character to the framebuffer console (foreground white)
void fb_putc(char c);
void fb_puts(const char* s);
// Erase previous character on the framebuffer console (handle backspace)
void fb_backspace(void);
// Show/hide blinking block cursor at current cursor position
void fb_cursor_show(void);
void fb_cursor_hide(void);
