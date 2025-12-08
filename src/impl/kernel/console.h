#pragma once

#include <stdint.h>

// Put a single character to the framebuffer console (foreground white)
void fb_putc(char c);
void fb_puts(const char* s);
