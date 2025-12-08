#include "stdio.h"
#include "ps2.h"
#include "console.h"

int getchar(void) {
    return ps2_getchar();
}

int putchar(int c) {
    if (c == '\n') fb_putc('\n');
    else fb_putc((char)c);
    return c;
}

void puts(const char* s) {
    fb_puts(s);
}
