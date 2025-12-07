#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

// --------------------------------------------------------------------------
// 1. BSD COMPATIBILITY & FONT (Solaris Look)
// --------------------------------------------------------------------------
typedef uint8_t   u_int8_t;
typedef uint8_t   u_char;
typedef uint16_t  u_int16_t;
typedef uint16_t  u_short;
typedef uint32_t  u_int32_t;
typedef uint32_t  u_int;

#define WSDISPLAY_FONTENC_ISO 0
#define WSDISPLAY_FONTORDER_L2R 0

struct wsdisplay_font {
    const char *name;
    int index;
    int firstchar;
    int numchars;
    int encoding;
    int fontwidth;
    int fontheight;
    int stride;
    int bitorder;
    int byteorder;
    void *cookie;
    void *data;
};

// Make sure this file exists!
#include "include/gallant12x22.h"

// --------------------------------------------------------------------------
// 2. HARDWARE I/O & BEEP
// --------------------------------------------------------------------------
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

void beep(void) {
    // 1. Play 1000Hz Sound
    uint32_t divisor = 1193180 / 1000;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(divisor & 0xFF));
    outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));
    uint8_t tmp = inb(0x61);
    if (tmp != (tmp | 3)) outb(0x61, tmp | 3);

    // 2. Wait (using volatile to prevent optimizer deletion)
    for (volatile uint32_t i = 0; i < 10000000; i++) {
        __asm__ volatile("nop");
    }

    // 3. Stop Sound
    tmp = inb(0x61);
    outb(0x61, tmp & 0xFC);
}

// --------------------------------------------------------------------------
// 3. GRAPHICS & TEXT
// --------------------------------------------------------------------------
#define PACKED __attribute__((packed))
#define FONT_WIDTH  12
#define FONT_HEIGHT 22
#define FONT_FIRST_CHAR 32

struct multiboot_tag { uint32_t type; uint32_t size; } PACKED;
struct multiboot_tag_framebuffer {
    struct multiboot_tag common;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint16_t reserved;
} PACKED;

uint8_t *fb_addr;
uint32_t fb_pitch, fb_width, fb_height;
uint8_t  fb_bpp;
uint32_t cursor_x = 0, cursor_y = 0;

void put_pixel(int x, int y, uint32_t color) {
    if (x >= fb_width || y >= fb_height) return;
    uint64_t offset = (y * fb_pitch) + (x * (fb_bpp / 8));
    *(volatile uint32_t *)(fb_addr + offset) = color;
}

void draw_char(char c, int x, int y, uint32_t color) {
    unsigned char uc = (unsigned char)c;
    if (uc < FONT_FIRST_CHAR) return;
    int index = uc - FONT_FIRST_CHAR;
    int offset = index * FONT_HEIGHT * 2;

    for (int row = 0; row < FONT_HEIGHT; row++) {
        uint8_t byte1 = gallant12x22_data[offset + (row * 2)];
        uint8_t byte2 = gallant12x22_data[offset + (row * 2) + 1];
        uint16_t line = (byte1 << 8) | byte2;

        for (int col = 0; col < FONT_WIDTH; col++) {
            if ((line >> (15 - col)) & 1) put_pixel(x + col, y + row, color);
        }
    }
}

void kprint(const char *str, uint32_t color) {
    for (int i = 0; str[i] != '\0'; i++) {
        char c = str[i];
        if (c == '\n') {
            cursor_x = 0;
            cursor_y += FONT_HEIGHT;
            continue;
        }
        draw_char(c, cursor_x, cursor_y, color);
        cursor_x += FONT_WIDTH;
        if (cursor_x >= fb_width - FONT_WIDTH) {
            cursor_x = 0;
            cursor_y += FONT_HEIGHT;
        }
    }
}

// --------------------------------------------------------------------------
// 4. PRINTF IMPLEMENTATION
// --------------------------------------------------------------------------
void reverse(char s[]) {
    int i, j;
    char c;
    for (i = 0, j = 0; s[j] != '\0'; j++);
    for (i = 0, j = j - 1; i < j; i++, j--) {
        c = s[i]; s[i] = s[j]; s[j] = c;
    }
}

void itoa(int64_t n, char s[], int base) {
    int i = 0;
    int sign = 0;
    uint64_t un;
    if (base == 10 && n < 0) { sign = 1; un = -n; } else { un = (uint64_t)n; }
    if (un == 0) { s[i++] = '0'; } // Handle 0 explicitly
    else {
        do {
            int digit = un % base;
            s[i++] = (digit > 9) ? (digit - 10) + 'A' : digit + '0';
        } while ((un /= base) > 0);
    }
    if (sign) s[i++] = '-';
    s[i] = '\0';
    reverse(s);
}

void kprintf(const char* format, uint32_t color, ...) {
    va_list args;
    va_start(args, color);

    for (int i = 0; format[i] != '\0'; i++) {
        if (format[i] == '%') {
            i++;
            switch (format[i]) {
                case 's': { 
                    kprint(va_arg(args, char*), color); 
                    break; 
                }
                case 'd': { // 32-bit Integer
                    int num = va_arg(args, int); 
                    char buffer[32];
                    itoa(num, buffer, 10);
                    kprint(buffer, color);
                    break;
                }
                case 'l': { // 64-bit Long
                    int64_t num = va_arg(args, int64_t);
                    char buffer[32];
                    itoa(num, buffer, 10);
                    kprint(buffer, color);
                    break;
                }
                case 'x': { // Hex (assume 64-bit address)
                    uint64_t num = va_arg(args, uint64_t);
                    char buffer[32];
                    itoa(num, buffer, 16);
                    kprint("0x", color);
                    kprint(buffer, color);
                    break;
                }
                case '%': kprint("%", color); break;
            }
        } else {
            char buffer[2] = {format[i], '\0'};
            kprint(buffer, color);
        }
    }
    va_end(args);
}

// --------------------------------------------------------------------------
// 5. KERNEL ENTRY
// --------------------------------------------------------------------------
void kernel_main(uint64_t addr) {
    struct multiboot_tag *tag = (struct multiboot_tag *)(addr + 8);
    struct multiboot_tag_framebuffer *fb = 0;

    while (tag->type != 0) {
        if (tag->type == 8) { fb = (struct multiboot_tag_framebuffer *)tag; break; }
        tag = (struct multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7));
    }

    if (fb == 0) return;

    fb_addr = (uint8_t *)fb->framebuffer_addr;
    fb_width = fb->framebuffer_width;
    fb_height = fb->framebuffer_height;
    fb_pitch = fb->framebuffer_pitch;
    fb_bpp = fb->framebuffer_bpp;

    // Clear Screen
    for (uint32_t y = 0; y < fb_height; y++) {
        for (uint32_t x = 0; x < fb_width; x++) {
            put_pixel(x, y, 0xFF000000);
        }
    }

    cursor_x = 0; cursor_y = 0;
    
    beep();
    
    // --- TEST 4: Solaris Banner ---
    kprint("---- KERNEL START ENTRY ----\n", 0x00FF0000);
    kprint("\nRhoudveine OS PRE-ALPHA Release Alpha-0.002 64-bit\n", 0xFFFFFFFF);
    kprint("Copyright (c) 2025, 2027, Cibi.inc, Altec and/or its affiliates.\n", 0xFFFFFFFF);
    kprint("Hostname: localhost\n\n", 0xFFFFFFFF);
    kprint("64 BIT HOST DETECTED !", 0xFFFFFFFF);
    kprint("\n---- KERNEL START INFORMATION ----\n", 0x00FF0000);
    kprintf("Framebuffer: %x\n", 0x00FF0000, addr);

    while(1) { __asm__("hlt"); }
}