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

#include "include/beep.h"
#include "idt.h"
#include "fat32.h"
#include "elf.h"
#include "serial.h"


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

// Multiboot module tag (type 3)
struct multiboot_tag_module {
    struct multiboot_tag common;
    uint32_t mod_start;
    uint32_t mod_end;
    char cmdline[0];
} PACKED;

// forward declare kprint/kprintf so helper can use them before their definitions
void kprint(const char *str, uint32_t color);
void kprintf(const char *format, uint32_t color, ...);

static void print_mod_info(struct multiboot_tag_module *m) {
    if (!m) return;
    char *cmd = (char*)(&m->cmdline[0]);
    kprint("Found module: ", 0x00FF0000);
    kprint(cmd, 0xFFFFFFFF);
    kprint("\n", 0xFFFFFFFF);
}

uint8_t *fb_addr;
uint32_t fb_pitch, fb_width, fb_height;
uint8_t  fb_bpp;
uint32_t cursor_x = 0, cursor_y = 0;
// small helper to compare strings (file-scope)
static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';

}

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
            // mirror newline to serial as CRLF
            serial_putc('\r');
            serial_putc('\n');
            continue;
        }
        draw_char(c, cursor_x, cursor_y, color);
        // mirror character to serial
        serial_putc(c);
        cursor_x += FONT_WIDTH;
        if (cursor_x >= fb_width - FONT_WIDTH) {
            cursor_x = 0;
            cursor_y += FONT_HEIGHT;
        }
    }
}

// Simple framebuffer console helpers exported for other modules
void fb_putc(char c) {
    char buf[2] = {c, '\0'};
    kprint(buf, 0xFFFFFFFF);
    serial_putc(c);
}

void fb_puts(const char* s) {
    kprint(s, 0xFFFFFFFF);
    serial_write(s);
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
    struct multiboot_tag_module *mod = 0;

    /* Desired init path */
    const char *init_path = "/System/Rhoudveine/Booter/init";
    int found_init = 0;

    // Symbols provided by objcopy -I binary when embedding the init ELF
    extern unsigned char _binary_build_init_init_elf_start[];
    extern unsigned char _binary_build_init_init_elf_end[];
    // If the init object was linked directly into the kernel, it will expose
    // a `main` symbol we can call directly. Declare it here.
    extern void main(void (*print_fn)(const char*));

    

    /* First pass: discover tags and check module cmdlines */
    while (tag->type != 0) {
        if (tag->type == 8) { fb = (struct multiboot_tag_framebuffer *)tag; }
        if (tag->type == 3) {
            struct multiboot_tag_module *m = (struct multiboot_tag_module *)tag;
            print_mod_info(m);
            char *cmd = (char*)(&m->cmdline[0]);
            if (cmd && cmd[0] == '/') {
                if (str_eq(cmd, init_path)) found_init = 1;
            }
        }
        tag = (struct multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7));
    }

    /* Second pass: try each module as a FAT32 image and search inside */
    tag = (struct multiboot_tag *)(addr + 8);
    struct fat32_fs fs;
    for (; tag->type != 0; tag = (struct multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7))) {
        if (tag->type == 3) {
            struct multiboot_tag_module *m = (struct multiboot_tag_module *)tag;
            uint8_t *start = (uint8_t*)(uintptr_t)m->mod_start;
            uint32_t len = m->mod_end - m->mod_start;
            if (fat32_init_from_memory(&fs, start, len) == 0) {
                uint8_t *fileptr = NULL;
                uint32_t filesize = 0;
                if (fat32_open_file(&fs, init_path, &fileptr, &filesize) == 0) {
                    kprint("Found init inside FAT32 module: ", 0x00FF0000);
                    kprint(init_path, 0xFFFFFFFF);
                    kprint(" size=", 0xFFFFFFFF);
                    kprintf("%l", 0xFFFFFFFF, (uint64_t)filesize);
                    kprint("\n", 0xFFFFFFFF);
                    found_init = 1;
                    break;
                }
            }
        }
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
    
    init_idt();
    beep(1000, 5000000000);
    // initialize serial so we can capture kernel output on COM1
    serial_init();


    
    // --- TEST 4: Solaris Banner ---
    kprint("---- KERNEL START ENTRY ----\n", 0x00FF0000);
    kprint("\nRhoudveine OS PRE-ALPHA Release Alpha-0.002 64-bit\n", 0xFFFFFFFF);
    kprint("Copyright (c) 2025, 2027, Cibi.inc, Altec and/or its affiliates.\n", 0xFFFFFFFF);
    kprint("Hostname: localhost\n\n", 0xFFFFFFFF);
    kprint("64 BIT HOST DETECTED !", 0xFFFFFFFF);
    kprint("\n---- KERNEL START INFORMATION ----\n", 0x00FF0000);
    kprintf("Framebuffer: %x\n", 0x00FF0000, addr);

    // If init was not found in modules, fall back to the embedded init ELF
    if (!found_init) {
        kprint("init module not found in multiboot modules; attempting embedded fallback\n", 0x00FF0000);
        // If `main` was linked into the kernel (init_obj included), call it directly.
        // This avoids ELF loading complexity for the fallback case.
        if (main) {
            kprint("Calling embedded init main()...\n", 0x00FF0000);
            main(fb_puts);
            // If main returns for some reason, halt.
            kprint("Embedded init returned unexpectedly\n", 0x00FF0000);
            while (1) { __asm__("hlt"); }
        }

        // As a last resort, try the binary-embedded ELF (if present) via ELF loader.
        unsigned char *start = _binary_build_init_init_elf_start;
        unsigned char *end = _binary_build_init_init_elf_end;
        uintptr_t len = (uintptr_t)(end - start);
        if (len > 4) {
            kprint("Found embedded init ELF, attempting to run via ELF loader...\n", 0x00FF0000);
            int r = elf64_load_and_run(start, (uint32_t)len, fb_puts);
            if (r != 0) {
                kprint("Failed to run embedded init (elf loader)\n", 0x00FF0000);
                while (1) { __asm__("hlt"); }
            }
        }

        kprint("KERNEL PANIC: init not found\n", 0x00FF0000);
        while (1) { __asm__("hlt"); }
    }

    // If we reach here, try to find the matching module and run it as an ELF.
    tag = (struct multiboot_tag *)(addr + 8);
    for (; tag->type != 0; tag = (struct multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7))) {
        if (tag->type == 3) {
            struct multiboot_tag_module *m = (struct multiboot_tag_module *)tag;
            char *cmd = (char*)(&m->cmdline[0]);
            if (cmd && cmd[0] == '/' && str_eq(cmd, init_path)) {
                uint8_t *start = (uint8_t*)(uintptr_t)m->mod_start;
                uint32_t len = m->mod_end - m->mod_start;
                kprint("Loading init module...\n", 0x00FF0000);
                int r = elf64_load_and_run(start, len, fb_puts);
                if (r != 0) {
                    kprint("Failed to run init (elf loader)\n", 0x00FF0000);
                    while (1) { __asm__("hlt"); }
                }
            }
        }
    }

    while(1) { __asm__("hlt"); }
}