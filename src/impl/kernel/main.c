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
#include "init_fs.h"
#include "panic.h"
#include "vray.h"


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

// Multiboot string tag (type 1)
struct multiboot_tag_string {
    struct multiboot_tag common;
    char string[0];
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
// If set, suppress all framebuffer drawing (still mirror to serial)
static int suppress_fb = 0;
// small helper to compare strings (file-scope)
static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';

}

// contains substring (simple)
static int contains_substr(const char *s, const char *sub) {
    if (!s || !sub) return 0;
    for (int i = 0; s[i]; i++) {
        int j = 0;
        while (sub[j] && s[i+j] && s[i+j] == sub[j]) j++;
        if (!sub[j]) return 1;
    }
    return 0;
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
            if (!suppress_fb) {
                cursor_x = 0;
                cursor_y += FONT_HEIGHT;
                // scroll if we've reached the bottom
                if ((uint32_t)cursor_y >= fb_height) {
                    // move framebuffer up by FONT_HEIGHT rows
                    uint32_t rows_to_move = fb_height - FONT_HEIGHT;
                    uint32_t row_bytes = fb_pitch;
                    uint8_t *src = fb_addr + (FONT_HEIGHT * row_bytes);
                    uint8_t *dst = fb_addr;

                    // Fast path for 32bpp framebuffers: copy 32-bit words per row.
                    if (fb_bpp == 32 && (row_bytes % 4) == 0) {
                        uint32_t words_per_row = row_bytes / 4;
                        for (uint32_t row = 0; row < rows_to_move; row++) {
                            uint32_t *s = (uint32_t*)(src + row * row_bytes);
                            uint32_t *d = (uint32_t*)(dst + row * row_bytes);
                            for (uint32_t w = 0; w < words_per_row; w++) d[w] = s[w];
                        }
                        // clear the bottom FONT_HEIGHT rows quickly
                        uint32_t *base = (uint32_t*)(fb_addr + rows_to_move * row_bytes);
                        uint32_t words_per_line = fb_pitch / 4;
                        for (uint32_t y = 0; y < FONT_HEIGHT; y++) {
                            for (uint32_t x = 0; x < fb_width; x++) {
                                base[y * words_per_line + x] = 0xFF000000;
                            }
                        }
                    } else {
                        // Fallback byte-wise copy
                        uint32_t bytes_to_move = rows_to_move * row_bytes;
                        for (uint32_t b = 0; b < bytes_to_move; b++) dst[b] = src[b];
                        // clear the bottom FONT_HEIGHT rows using put_pixel
                        for (uint32_t y = fb_height - FONT_HEIGHT; y < fb_height; y++) {
                            for (uint32_t x = 0; x < fb_width; x++) put_pixel(x, y, 0xFF000000);
                        }
                    }

                    cursor_y = fb_height - FONT_HEIGHT;
                }
            }
            // mirror newline to serial as CRLF
            serial_putc('\r');
            serial_putc('\n');
            continue;
        }
        if (!suppress_fb) draw_char(c, cursor_x, cursor_y, color);
        // mirror character to serial
        serial_putc(c);
        if (!suppress_fb) {
            cursor_x += FONT_WIDTH;
            if (cursor_x >= fb_width - FONT_WIDTH) {
                cursor_x = 0;
                cursor_y += FONT_HEIGHT;
            }
        }
    }
}

// Simple framebuffer console helpers exported for other modules
void fb_putc(char c) {
    // Force framebuffer output even when kernel is in quiet mode
    int old = suppress_fb;
    suppress_fb = 0;
    char buf[2] = {c, '\0'};
    kprint(buf, 0xFFFFFFFF);
    suppress_fb = old;
    serial_putc(c);
}

void fb_puts(const char* s) {
    // Force framebuffer output even when kernel is in quiet mode
    int old = suppress_fb;
    suppress_fb = 0;
    kprint(s, 0xFFFFFFFF);
    suppress_fb = old;
    serial_write(s);
}

// Erase the previous character on the framebuffer console. This attempts
// to move the cursor back one character and overwrite with a space.
void fb_backspace(void) {
    if (cursor_x >= FONT_WIDTH) {
        cursor_x -= FONT_WIDTH;
    } else {
        // move up one line if possible
        if (cursor_y >= FONT_HEIGHT) {
            cursor_y -= FONT_HEIGHT;
            cursor_x = fb_width - FONT_WIDTH;
        } else {
            cursor_x = 0;
            // nothing to erase
            return;
        }
    }
    // clear the character cell by filling with background color
    for (int y = 0; y < FONT_HEIGHT; y++) {
        for (int x = 0; x < FONT_WIDTH; x++) {
            put_pixel(cursor_x + x, cursor_y + y, 0xFF000000);
        }
    }
    // mirror to serial as backspace+space+backspace for terminal viewers
    serial_putc('\b'); serial_putc(' '); serial_putc('\b');
}

// Cursor saved pixels for blinking (store as 32-bit ARGB values)
static uint32_t cursor_saved[FONT_WIDTH * FONT_HEIGHT];
static int cursor_saved_valid = 0;
static int cursor_visible = 0;

void fb_cursor_show(void) {
    if (suppress_fb) return;
    if (cursor_visible) return;
    if (!fb_addr) return;
    int w = FONT_WIDTH;
    int h = FONT_HEIGHT;
    uint8_t *base = fb_addr;
    uint32_t bpp_bytes = fb_bpp / 8;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint64_t offset = (uint64_t)(cursor_y + y) * fb_pitch + (cursor_x + x) * bpp_bytes;
            uint32_t *pixel = (uint32_t *)(base + offset);
            uint32_t val = *pixel;
            cursor_saved[y * w + x] = val;
            // invert RGB (preserve alpha)
            uint32_t inv = (~val & 0x00FFFFFF) | (val & 0xFF000000);
            *pixel = inv;
        }
    }
    cursor_saved_valid = 1;
    cursor_visible = 1;
}

void fb_cursor_hide(void) {
    if (suppress_fb) return;
    if (!cursor_visible) return;
    if (!fb_addr || !cursor_saved_valid) return;
    int w = FONT_WIDTH;
    int h = FONT_HEIGHT;
    uint8_t *base = fb_addr;
    uint32_t bpp_bytes = fb_bpp / 8;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint64_t offset = (uint64_t)(cursor_y + y) * fb_pitch + (cursor_x + x) * bpp_bytes;
            uint32_t *pixel = (uint32_t *)(base + offset);
            *pixel = cursor_saved[y * w + x];
        }
    }
    cursor_visible = 0;
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
    extern unsigned char _binary_build_init_init_elf_start[] __attribute__((weak));
    extern unsigned char _binary_build_init_init_elf_end[] __attribute__((weak));
    // If the init object was linked directly into the kernel, it will expose
    // a `main` symbol we can call directly. Declare it here.
    extern void main(void (*print_fn)(const char*));

    

    /* First pass: discover tags, check module cmdlines and parse cmdline */
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
        if (tag->type == 1) {
            struct multiboot_tag_string *s = (struct multiboot_tag_string *)tag;
            char *cmdline = (char*)(&s->string[0]);
            if (cmdline && contains_substr(cmdline, "quiet")) {
                suppress_fb = 1;
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
                // Record the first discovered FAT32 module image for embedded init
                // utilities to use when the init code is linked into the kernel.
                embedded_fat32_image = start;
                embedded_fat32_size = len;
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

    // Initialize VRAY (PCI) subsystem and enumerate devices
    vray_init();


    
    // --- TEST 4: Solaris Banner ---
    // Ensure the main banner is always visible even when `quiet` is set.
    {
        int old = suppress_fb;
        suppress_fb = 0;
        kprint("---- KERNEL START ENTRY ----\n", 0x00FF0000);
        kprint("\nRhoudveine OS PRE-ALPHA Release Alpha-0.002 64-bit\n", 0xFFFFFFFF);
        kprint("Copyright (c) 2025, 2027, Cibi.inc, Altec and/or its affiliates.\n", 0xFFFFFFFF);
        kprint("Hostname: localhost\n\n", 0xFFFFFFFF);
        kprint("64 BIT HOST DETECTED !", 0xFFFFFFFF);
        kprint("\n---- KERNEL START INFORMATION ----\n", 0x00FF0000);
        kprintf("Framebuffer: %x\n", 0x00FF0000, addr);
        suppress_fb = old;
    }

    // If init was not found in modules, fall back to the embedded init ELF
    if (!found_init) {
        kprint("init module not found in multiboot modules; attempting embedded fallback\n", 0x00FF0000);
        // If `main` was linked into the kernel (init_obj included), call it directly.
        // This avoids ELF loading complexity for the fallback case.
        if (main) {
            kprint("Calling embedded init main()...\n", 0x00FF0000);
            // Enable interrupts so embedded init can receive keyboard IRQs
            __asm__("sti");
            main(fb_puts);
            // If main returns for some reason, drop into panic shell and show state
            kprintf("Embedded init returned unexpectedly\n", 0x00FF0000);
            kernel_panic_shell("Embedded init returned unexpectedly");
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
                    kernel_panic_shell("Failed to run embedded init (elf loader)");
                }
            }

        kprint("KERNEL PANIC: init not found\n", 0x00FF0000);
        kernel_panic_shell("init not found");
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
                    kernel_panic_shell("Failed to run init (elf loader)");
                }
            }
        }
    }

    while(1) { __asm__("hlt"); }
}