#include <stdint.h>
#include <stddef.h> 

// --- 1. BSD COMPATIBILITY LAYER ---
typedef uint8_t   u_int8_t;
typedef uint8_t   u_char;
typedef uint16_t  u_int16_t;
typedef uint16_t  u_short;
typedef uint32_t  u_int32_t;
typedef uint32_t  u_int;

// --- 2. FAKE BSD STRUCTURES ---
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

// --- 3. INCLUDE THE ORIGINAL FILE ---
// Ensure this path matches your file structure!
#include "include/gallant12x22.h"

// --- 4. KERNEL DEFINITIONS ---
#define PACKED __attribute__((packed))
#define FONT_WIDTH  12
#define FONT_HEIGHT 22
#define FONT_FIRST_CHAR 32 // The OpenBSD file starts at Space (32)

struct multiboot_tag {
    uint32_t type;
    uint32_t size;
} PACKED;

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

// --- GLOBAL STATE ---
uint8_t *fb_addr;
uint32_t fb_pitch;
uint32_t fb_width;
uint32_t fb_height;
uint8_t  fb_bpp;
uint32_t cursor_x = 0;
uint32_t cursor_y = 0;

void put_pixel(int x, int y, uint32_t color) {
    if (x >= fb_width || y >= fb_height) return;
    uint64_t offset = (y * fb_pitch) + (x * (fb_bpp / 8));
    *(volatile uint32_t *)(fb_addr + offset) = color;
}

void draw_char(char c, int x, int y, uint32_t color) {
    // 1. Cast to unsigned to handle extended ASCII safely
    unsigned char uc = (unsigned char)c;

    // 2. Safety Check: The font array starts at 32 (Space).
    // If we try to draw a control character (< 32), we must exit 
    // or we will read invalid memory before the array starts.
    if (uc < FONT_FIRST_CHAR) return;

    // 3. Calculate Index: Subtract 32 to align 'A' (65) to index 33.
    int index = uc - FONT_FIRST_CHAR;

    // 4. Calculate Offset
    // Each character is 22 rows.
    // The OpenBSD file stores data as bytes (u_char).
    // Stride is 2 bytes (16 bits) per row.
    int offset = index * FONT_HEIGHT * 2; 

    for (int row = 0; row < FONT_HEIGHT; row++) {
        // Read 2 bytes from the array to form the 12-pixel row
        uint8_t byte1 = gallant12x22_data[offset + (row * 2)];
        uint8_t byte2 = gallant12x22_data[offset + (row * 2) + 1];
        
        // Combine them (Big Endian standard for font maps)
        uint16_t line = (byte1 << 8) | byte2;

        for (int col = 0; col < FONT_WIDTH; col++) {
            // Check bit 15 (leftmost) down to bit 4
            if ((line >> (15 - col)) & 1) {
                put_pixel(x + col, y + row, color);
            }
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
        
        // Wrap text
        if (cursor_x >= fb_width - FONT_WIDTH) {
            cursor_x = 0;
            cursor_y += FONT_HEIGHT;
        }
    }
}

void kernel_main(uint64_t addr) {
    struct multiboot_tag *tag = (struct multiboot_tag *)(addr + 8);
    struct multiboot_tag_framebuffer *fb = 0;

    while (tag->type != 0) {
        if (tag->type == 8) {
            fb = (struct multiboot_tag_framebuffer *)tag;
            break;
        }
        tag = (struct multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7));
    }

    if (fb == 0) return;

    fb_addr = (uint8_t *)fb->framebuffer_addr;
    fb_width = fb->framebuffer_width;
    fb_height = fb->framebuffer_height;
    fb_pitch = fb->framebuffer_pitch;
    fb_bpp = fb->framebuffer_bpp;

    // Clear Screen (Black)
    for (uint32_t y = 0; y < fb_height; y++) {
        for (uint32_t x = 0; x < fb_width; x++) {
            put_pixel(x, y, 0xFF000000); 
        }
    }

    cursor_x = 20;
    cursor_y = 20;


    kprint("---- KERNEL START ENTRY ----\n", 0x00FF0000);
    kprint("Rhoudveine OS PRE-ALPHA 0.12-testing-aaaaax 64-bit\n", 0xFFFFFFFF);
    kprint("Copyright (c) 2025, 2027, Cibi.inc, Altec and/or its affiliates.\n", 0xFFFFFFFF);
    kprint("64-bit Host\n\n", 0xFFFFFFFF);
    kprint("---- KERNEL INFORMATION ----", 0x00FF0000);

    while(1) { __asm__("hlt"); }
}