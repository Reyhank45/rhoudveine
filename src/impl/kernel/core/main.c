#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

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

// The Solaris Gallant font
#include "include/gallant12x22.h"
#include "include/beep.h"
#include "include/idt.h"
#include "fs/fat32.h"
#include "include/elf.h"
#include "include/serial.h"
#include "include/init_fs.h"
#include "include/panic.h"
#include "include/vray.h"
#include "include/autoconf.h"
#include "include/vnode.h"
#include "include/nvnode.h"
#include "include/acpi.h"
#include "include/usb.h"
#include "include/mm.h"
#include "include/timer.h"
#include "include/ahci.h"
#include "include/vfs.h"
#include "include/fat32_vfs.h"
#include "include/devfs.h"
#include "include/procfs.h"
#include "include/ramfs.h"
#include "include/ps2.h"


// --------------------------------------------------------------------------
// 3. GRAPHICS & TEXT
// --------------------------------------------------------------------------
#define PACKED __attribute__((packed))
#define FONT_WIDTH  12
#define FONT_HEIGHT 22
#define FONT_FIRST_CHAR 32

#define FB_BG_COLOR 0xFF000000 // Opaque black

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
// Static backbuffer in BSS for double buffering (max 1920x1200x4 = ~9MB)
// Using static allocation guarantees contiguous memory
#define FB_BACKBUFFER_MAX_SIZE (1920 * 1200 * 4)
static uint8_t fb_backbuffer_static[FB_BACKBUFFER_MAX_SIZE] __attribute__((aligned(4096)));
uint8_t *fb_backbuffer = NULL;  // Points to static buffer if enabled
uint32_t fb_pitch, fb_width, fb_height;
uint8_t  fb_bpp;
uint32_t fb_size = 0;           // Total framebuffer size in bytes
uint32_t cursor_x = 0, cursor_y = 0;
// If set, suppress all framebuffer drawing (still mirror to serial)
static int suppress_fb = 0;
// Track if backbuffer needs flushing
static int fb_dirty = 0;
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

// Helper to extract value from key=value in cmdline
// Returns 1 if key found, 0 otherwise
static int get_cmdline_arg(const char *cmdline, const char *key, char *value_buf, int buf_len) {
    if (!cmdline || !key || !value_buf) return 0;
    
    int key_len = 0;
    while (key[key_len]) key_len++;

    const char *p = cmdline;
    while (*p) {
        // skip whitespace
        while (*p == ' ') p++;
        if (!*p) break;

        // check if this token matches key
        int match = 1;
        for (int i = 0; i < key_len; i++) {
            if (p[i] != key[i]) {
                match = 0;
                break;
            }
        }

        // Must match key AND be followed by '='
        if (match && p[key_len] == '=') {
            p += key_len + 1; // skip key and '='
            int i = 0;
            while (*p && *p != ' ' && i < buf_len - 1) {
                value_buf[i++] = *p++;
            }
            value_buf[i] = '\0';
            return 1;
        }

        // skip to next token
        while (*p && *p != ' ') p++;
    }
    return 0;
}

void put_pixel(int x, int y, uint32_t color) {
    if (x >= fb_width || y >= fb_height) return;
    uint64_t offset = (y * fb_pitch) + (x * (fb_bpp / 8));
    // Write to backbuffer (fast RAM) instead of direct framebuffer (slow VRAM)
    if (fb_backbuffer) {
        *(uint32_t *)(fb_backbuffer + offset) = color;
    } else {
        *(volatile uint32_t *)(fb_addr + offset) = color;
    }
    fb_dirty = 1;
}

// Flush backbuffer to real framebuffer (call periodically or after major updates)
void fb_flush(void) {
    if (!fb_backbuffer || !fb_addr || !fb_dirty) return;
    
    // Use fast 64-bit copies
    uint64_t *src = (uint64_t *)fb_backbuffer;
    uint64_t *dst = (uint64_t *)fb_addr;
    uint32_t count = fb_size / 8;
    
    for (uint32_t i = 0; i < count; i++) {
        dst[i] = src[i];
    }
    
    fb_dirty = 0;
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
    // We'll render to framebuffer per-char (for cursor/scroll), but batch
    // serial output to avoid per-character port waits on slow hardware.
    // Iterate for framebuffer operations and handle scrolling, but defer
    // serial emission until after the loop.
    for (int i = 0; str[i] != '\0'; i++) {
        char c = str[i];
        if (c == '\n') {
            if (!suppress_fb) {
                cursor_x = 0;
                cursor_y += FONT_HEIGHT;
                // scroll if we've reached the bottom
                if ((uint32_t)cursor_y >= fb_height) {
                    uint32_t row_bytes = fb_pitch;
                    uint32_t rows_to_move = fb_height - FONT_HEIGHT;
                    // Use backbuffer for scroll if available (MUCH faster)
                    uint8_t *buffer = fb_backbuffer ? fb_backbuffer : fb_addr;
                    uint8_t *src = buffer + (FONT_HEIGHT * row_bytes);
                    uint8_t *dst = buffer;
                    size_t bytes_to_move = (size_t)rows_to_move * row_bytes;
                    // fast memmove for backbuffer (RAM is fast!)
                    if (bytes_to_move > 0) {
                        // prefer 64-bit copies (x86 allows unaligned access)
                        uint64_t *sd = (uint64_t*)dst;
                        uint64_t *ss = (uint64_t*)src;
                        size_t q = bytes_to_move / 8;
                        for (size_t t = 0; t < q; t++) sd[t] = ss[t];
                        size_t rem = bytes_to_move % 8;
                        uint8_t *bd = dst + q * 8;
                        uint8_t *bs = src + q * 8;
                        for (size_t t = 0; t < rem; t++) bd[t] = bs[t];
                    }

                    // clear the bottom FONT_HEIGHT rows efficiently when 32bpp
                    if (fb_bpp == 32 && (fb_pitch % 4) == 0) {
                        uint32_t *base = (uint32_t*)(buffer + (fb_height - FONT_HEIGHT) * fb_pitch);
                        uint32_t words_per_line = fb_pitch / 4;
                        for (uint32_t y = 0; y < FONT_HEIGHT; y++) {
                            for (uint32_t x = 0; x < words_per_line; x++) base[y * words_per_line + x] = FB_BG_COLOR;
                        }
                    } else {
                        for (uint32_t y = fb_height - FONT_HEIGHT; y < fb_height; y++) {
                            for (uint32_t x = 0; x < fb_width; x++) put_pixel(x, y, FB_BG_COLOR);
                        }
                    }

                    cursor_y = fb_height - FONT_HEIGHT;
                    fb_dirty = 1;
                }
            }
            continue;
        }
        if (!suppress_fb) draw_char(c, cursor_x, cursor_y, color);
        if (!suppress_fb) {
            cursor_x += FONT_WIDTH;
            if (cursor_x >= fb_width - FONT_WIDTH) {
                cursor_x = 0;
                cursor_y += FONT_HEIGHT;
            }
        }
    }

    // Emit the whole string to serial in one call to reduce overhead.
    serial_write(str);
}

// Simple framebuffer console helpers exported for other modules
void fb_putc(char c) {
    // Force framebuffer output even when kernel is in quiet mode
    int old = suppress_fb;
    suppress_fb = 0;
    char buf[2] = {c, '\0'};
    kprint(buf, 0xFFFFFFFF);
    suppress_fb = old;
    // Flush immediately for user input responsiveness
    fb_flush();
}

void fb_puts(const char* s) {
    // Force framebuffer output even when kernel is in quiet mode
    int old = suppress_fb;
    suppress_fb = 0;
    kprint(s, 0xFFFFFFFF);
    suppress_fb = old;
    // Flush after bulk output
    fb_flush();
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
            put_pixel(cursor_x + x, cursor_y + y, FB_BG_COLOR);
        }
    }
    fb_flush();  // Immediate visual feedback
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
    uint8_t *base = fb_backbuffer ? fb_backbuffer : fb_addr;
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
    fb_flush();
}

void fb_cursor_hide(void) {
    if (suppress_fb) return;
    if (!cursor_visible) return;
    if (!fb_addr || !cursor_saved_valid) return;
    int w = FONT_WIDTH;
    int h = FONT_HEIGHT;
    uint8_t *base = fb_backbuffer ? fb_backbuffer : fb_addr;
    uint32_t bpp_bytes = fb_bpp / 8;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint64_t offset = (uint64_t)(cursor_y + y) * fb_pitch + (cursor_x + x) * bpp_bytes;
            uint32_t *pixel = (uint32_t *)(base + offset);
            *pixel = cursor_saved[y * w + x];
        }
    }
    cursor_visible = 0;
    fb_flush();
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

void utoa(uint64_t n, char s[], int base) {
    int i = 0;
    if (n == 0) {
        s[i++] = '0';
    } else {
        do {
            int digit = n % base;
            s[i++] = (digit > 9) ? (digit - 10) + 'A' : digit + '0';
        } while ((n /= base) > 0);
    }
    s[i] = '\0';
    reverse(s);
}

void kprintf(const char* format, uint32_t color, ...) {
    va_list args;
    va_start(args, color);

    for (int i = 0; format[i] != '\0'; i++) {
        if (format[i] == '%') {
            i++;
            int is_long = 0;
            if (format[i] == 'l') {
                is_long = 1;
                i++;
            }

            switch (format[i]) {
                case 's': { 
                    kprint(va_arg(args, char*), color); 
                    break; 
                }
                case 'd': {
                    long long num = is_long ? va_arg(args, long long) : va_arg(args, int);
                    char buffer[32];
                    itoa(num, buffer, 10);
                    kprint(buffer, color);
                    break;
                }
                case 'u': {
                    uint64_t num = is_long ? va_arg(args, uint64_t) : va_arg(args, unsigned int);
                    char buffer[32];
                    utoa(num, buffer, 10);
                    kprint(buffer, color);
                    break;
                }
                case 'x': {
                    uint64_t num = is_long ? va_arg(args, uint64_t) : va_arg(args, uint32_t);
                    char buffer[32];
                    utoa(num, buffer, 16);
                    kprint("0x", color);
                    kprint(buffer, color);
                    break;
                }
                case '%': 
                    kprint("%", color); 
                    break;
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

    

    struct multiboot_tag_old_acpi *acpi_old = 0;
    struct multiboot_tag_new_acpi *acpi_new = 0;
    
    /* First pass: discover tags, check module cmdlines and parse cmdline */
    struct multiboot_tag *iter = (struct multiboot_tag *)(addr + 8);
    while (iter->type != 0) {
        if (iter->type == 8) { fb = (struct multiboot_tag_framebuffer *)iter; }
        if (iter->type == 14) { acpi_old = (struct multiboot_tag_old_acpi *)iter; }
        if (iter->type == 15) { acpi_new = (struct multiboot_tag_new_acpi *)iter; }
        
        if (iter->type == 3) {
            struct multiboot_tag_module *m = (struct multiboot_tag_module *)iter;
            print_mod_info(m);
            char *cmd = (char*)(&m->cmdline[0]);
            if (cmd && cmd[0] == '/') {
                if (str_eq(cmd, init_path)) found_init = 1;
            }
        }
        if (iter->type == 1) {
            struct multiboot_tag_string *s = (struct multiboot_tag_string *)iter;
            char *cmdline = (char*)(&s->string[0]);
            if (cmdline && contains_substr(cmdline, "quiet")) {
                suppress_fb = 1;
            }
        }
        iter = (struct multiboot_tag *)((uint8_t *)iter + ((iter->size + 7) & ~7));
    }

    /* VFS handles filesystem now - old FAT32 module search disabled */
    
    // ... (rest of function until acpi_init) ...
    // Note: I am replacing lines 466-484 and then jumping to 546-547
    // This tool call approach is tricky. I should split this into two replaces or overwrite the top block properly.
    // Actually, I can just rewrite the top block to extract tags into variables I declared.
    // The previous loop used 'tag' variable, I used 'iter' but 'tag' is available. 
    // Let's stick to using 'tag' variable as in original code.
    
    // REDOING CONTENT for safety:
    
    // Need to define structs for ACPI tags or just cast void* 
    // struct multiboot_tag_old_acpi { struct multiboot_tag common; uint8_t rsdp[0]; }
    // struct multiboot_tag_new_acpi { struct multiboot_tag common; uint8_t rsdp[0]; }
    
    void *acpi_rsdp_ptr = 0;

    while (tag->type != 0) {
        if (tag->type == 8) { fb = (struct multiboot_tag_framebuffer *)tag; }
        
        // ACPI Old (14)
        if (tag->type == 14) { 
             // RSDP is just the data after common tag
             // struct multiboot_tag { uint32_t type; uint32_t size; } is 8 bytes
             acpi_rsdp_ptr = (void*)((uint8_t*)tag + 8);
        }
        // ACPI New (15) - Prefer this if available
        if (tag->type == 15) { 
             acpi_rsdp_ptr = (void*)((uint8_t*)tag + 8);
        }

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

    /* VFS handles filesystem now - old FAT32 module search disabled
    // Second pass: try each module as a FAT32 image and search inside
    tag = (struct multiboot_tag *)(addr + 8);
    struct fat32_mem_fs fs;
    for (; tag->type != 0; tag = (struct multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7))) {
        if (tag->type == 3) {
            struct multiboot_tag_module *m = (struct multiboot_tag_module *)tag;
            uint8_t *start = (uint8_t*)(uintptr_t)m->mod_start;
            uint32_t len = m->mod_end - m->mod_start;
            if (fat32_init_from_memory(&fs, start, len) == 0) {
                embedded_fat32_image = start;
                embedded_fat32_size = len;
                uint8_t *fileptr = NULL;
                uint32_t filesize = 0;
                if (fat32_open_file(&fs, init_path, &fileptr, &filesize) == 0) {
                    kprint("Found init inside FAT32 module: ", 0x00FF0000);
                    kprint(init_path, 0xFFFFFFFF);
                    kprintf(" size=%ld", 0xFFFFFFFF, (uint64_t)filesize);
                    kprint("\n", 0xFFFFFFFF);
                    found_init = 1;
                    break;
                }
            }
        }
    }
    */

    if (fb == 0) return;

    fb_addr = (uint8_t *)fb->framebuffer_addr;
    fb_width = fb->framebuffer_width;
    fb_height = fb->framebuffer_height;
    fb_pitch = fb->framebuffer_pitch;
    fb_bpp = fb->framebuffer_bpp;

    // Clear Screen
    for (uint32_t y = 0; y < fb_height; y++) {
        for (uint32_t x = 0; x < fb_width; x++) {
            put_pixel(x, y, FB_BG_COLOR);
        }
    }

    cursor_x = 0; cursor_y = 0;
    
    init_idt();
    beep(30000000, 1000, true);
    // initialize serial so we can capture kernel output on COM1
    serial_init();

    // Initialize memory manager
    mm_init(addr);

    // Enable framebuffer double buffering using static BSS buffer
    kprintf("FB: 1-starting init, fb_addr=0x%lx, pitch=%u, h=%u\n", 0x00FF00, 
            (uint64_t)fb_addr, fb_pitch, fb_height);
    fb_size = (uint32_t)fb_pitch * fb_height;
    kprintf("FB: 2-fb_size=%u, max=%u\n", 0x00FF00, fb_size, FB_BACKBUFFER_MAX_SIZE);
    
    // TEMPORARILY DISABLED: Double buffering causes hang on some real hardware
    // The issue is likely that fb_backbuffer_static is in BSS at a high address
    // that's not properly accessible. Skipping for now.
    kprintf("FB: Double buffering DISABLED for debugging\n", 0xFFFF00);
    fb_backbuffer = NULL;  // Don't use backbuffer

    // Initialize PIT timer (100 Hz)
    kprintf("Initializing timer...\n", 0x00FF0000);
    pit_init(100);  // 100 ticks per second

    // Initialize scheduler (SMP)
    #ifdef CONFIG_SMP
    extern void sched_init(void);
    sched_init();
    #endif

    // Initialize CPU frequency scaling
    #ifdef CONFIG_CPU_FREQ
    extern void cpufreq_init(void);
    cpufreq_init();
    #endif

    // Initialize block layer and I/O schedulers
    extern void blk_init(void);
    blk_init();

    // Initialize syscall handler
    extern void syscall_init(void);
    syscall_init();

    kprintf("Initializing device subsystems...\n", 0x00FF0000);
    #ifdef CONFIG_VNODE
    vnode_init();
    #endif
    
    #ifdef CONFIG_NVNODE
    nvnode_init();
    #endif
    
    // Explicitly initialize PS/2 controller (keyboard)
    #ifdef CONFIG_PS2
    kprintf("Initializing PS/2 Controller...\n", 0x00FF0000);
    ps2_init();
    #endif
    
    #ifdef CONFIG_ACPI
    kprintf("Initializing ACPI...\n", 0x00FF0000);
    acpi_init(acpi_rsdp_ptr);
    #endif
    
    #ifdef CONFIG_VRAY
    kprintf("Initializing VRAY (PCI)...\n", 0x00FF0000);
    // Initialize VRAY (PCI) subsystem and enumerate devices
    vray_init();
    #endif
    
    kprintf("Initializing AHCI...\n", 0x00FF0000);
    // Initialize AHCI driver for SATA disks
    ahci_init();
    
    // Initialize USB HID
    #ifdef CONFIG_USB_HID
    extern void usb_hid_init(void);
    usb_hid_init();
    #endif
    
    #ifdef CONFIG_VFS
    kprintf("Initializing VFS...\n", 0x00FF0000);
    vfs_init();
    
    #ifdef CONFIG_FAT32
    kprintf("Registering FAT32 filesystem...\n", 0x00FF0000);
    fat32_register();
    #endif
    
    // Initialize and mount runtime filesystems
    kprintf("Initializing Runtime Filesystems...\n", 0x00FF0000);
    devfs_register();
    procfs_register();
    
    #ifdef CONFIG_RAMFS
    ramfs_register();
    #endif
    #endif

    // Reset tag pointer to start for cmdline parsing
    char root_dev[64];
    root_dev[0] = '\0';
    tag = (struct multiboot_tag *)(addr + 8);
    while (tag->type != 0) {
        if (tag->type == 1) { // MULTIBOOT_TAG_TYPE_CMDLINE
            struct multiboot_tag_string *s = (struct multiboot_tag_string *)tag;
            char *cmdline = (char*)(&s->string[0]);
            get_cmdline_arg(cmdline, "root", root_dev, sizeof(root_dev));
        }
        tag = (struct multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7));
    }

    if (root_dev[0] != '\0') {
        kprintf("Mounting root filesystem from %s...\n", 0x00FF0000, root_dev);
        if (vfs_mount("/", "fat32", root_dev) == 0) {
            kprintf("VFS: Root filesystem mounted successfully (FAT32)\n", 0x00FF0000);
        } else {
            kprintf("VFS: Failed to mount root filesystem on %s\n", 0xFFFF0000, root_dev);
            // Fallback? Assuming explicit fail for now if user asked for it but it failed.
        }
    } else {
        kprintf("VFS: No root= argument, defaulting to ramfs root...\n", 0xFFFFFF00);
        kprintf("DEBUG: About to call vfs_mount for ramfs\n", 0x00FFFF00);
        int result = vfs_mount("/", "ramfs", "none");
        kprintf("DEBUG: vfs_mount returned %d\n", 0x00FFFF00, result);
        if (result == 0) {
            kprintf("VFS: RamFS mounted as root\n", 0x00FF0000);
        } else {
            kprintf("VFS: Failed to mount RamFS root\n", 0xFFFF0000);
        }
    }

    kprintf("DEBUG: About to create directory structure\n", 0x00FFFF00);
    // Create runtime directory structure
    // Now that root is (hopefully) mounted, we create the structure
    
    // Create /System if not exists (in case it wasn't there)
    kprintf("DEBUG: Creating /System\n", 0x00FFFF00);
    vfs_mkdir("/System");
    kprintf("DEBUG: Creating /System/Rhoudveine\n", 0x00FFFF00);
    vfs_mkdir("/System/Rhoudveine");
    kprintf("DEBUG: Creating /System/Rhoudveine/Runtime\n", 0x00FFFF00);
    vfs_mkdir("/System/Rhoudveine/Runtime");
    
    // Create mount points
    kprintf("DEBUG: Creating mount points\n", 0x00FFFF00);
    vfs_mkdir("/System/Rhoudveine/Runtime/Device");
    vfs_mkdir("/System/Rhoudveine/Runtime/Process");

    // Mount devfs (DeviceFS)
    if (vfs_mount("/System/Rhoudveine/Runtime/Device", "DeviceFS", "none") == 0) {
        kprintf("Mounted Device filesystem.\n", 0x00FF0000);
        
        // Populate with PCI devices (stub - functions will add later)
        extern void devfs_add_device(const char *name, void *device_data);
        devfs_add_device("ahci0", NULL);
        devfs_add_device("vga0", NULL);
        devfs_add_device("eth0", NULL);
        devfs_add_device("cpu0", NULL);
        
        kprintf("DeviceFS: Populated with device stubs\n", 0x00FFFF00);
    } else {
        kprintf("Failed to mount Device filesystem.\n", 0xFFFF0000);
    }

    // Mount procfs (ProcessFS)
    if (vfs_mount("/System/Rhoudveine/Runtime/Process", "ProcessFS", "none") == 0) {
        kprintf("Mounted Process filesystem.\n", 0x00FF0000);
        
        // Add init process entry
        extern void procfs_add_entry(const char *name, const char *content);
        procfs_add_entry("init", "PID: 1\nName: init\nState: Running\n");
        
        // Get real memory stats
        extern uint64_t mm_get_total_memory(void);
        extern uint64_t mm_get_free_memory(void);
        uint64_t total_mb = mm_get_total_memory() / (1024 * 1024);
        uint64_t free_mb = mm_get_free_memory() / (1024 * 1024);
        
        // Format meminfo with real values
        char meminfo_buf[128];
        extern int sprintf(char *buf, const char *fmt, ...);
        sprintf(meminfo_buf, "MemTotal: %lu MB\nMemFree: %lu MB\n", total_mb, free_mb);
        procfs_add_entry("meminfo", meminfo_buf);
        
        // Get real CPU count from ACPI
        extern int acpi_cpu_count;
        int cpu_cores = acpi_cpu_count > 0 ? acpi_cpu_count : 1;
        
        // Format cpuinfo with real values
        char cpuinfo_buf[128];
        sprintf(cpuinfo_buf, "CPU: x86_64\nCores: %d\n", cpu_cores);
        procfs_add_entry("cpuinfo", cpuinfo_buf);
        
        kprintf("ProcessFS: Populated with real system info\n", 0x00FFFF00);
    } else {
        kprintf("Failed to mount Process filesystem.\n", 0xFFFF0000);
    }
    
    #ifdef CONFIG_XHCI
    kprintf("Initializing USB stack...\n", 0x00FF0000);
    // Note: This may hang on some hardware - wrap in safety check later
    usb_init(); // Initializes USB controllers like xHCI, which depends on VRAY
    #endif
    
    kprintf("Populating VNodes from PCI...\n", 0x00FF0000);
    #ifdef CONFIG_VNODE
    vnode_populate_from_pci();
    #endif
    
    #ifdef CONFIG_NVNODE
    nvnode_populate_from_pci();
    #endif
    
    #ifdef CONFIG_VNODE
    vnode_dump_list();
    #endif
    
    #ifdef CONFIG_NVNODE
    nvnode_dump_list();
    #endif


    
    // --- TEST 4: Solaris Banner ---
    // Ensure the main banner is always visible even when `quiet` is set.
    {
        int old = suppress_fb;
        suppress_fb = 0;
        kprint("---- KERNEL START ENTRY ----\n", 0x00FF0000);
        kprint("\nRhoudveine OS PRE-ALPHA Release Alpha-0.004 64-bit\n", 0xFFFFFFFF);
        kprint("Copyright (c) 2025, 2027, Cibi.inc, Altec and/or its affiliates.\n", 0xFFFFFFFF);
        kprint("Hostname: localhost\n\n", 0xFFFFFFFF);
        kprint("64 BIT HOST DETECTED !", 0xFFFFFFFF);
        kprint("\n---- KERNEL START INFORMATION ----\n", 0x00FF0000);
        kprintf("Framebuffer: %lx\n", 0x00FF0000, addr);
        suppress_fb = old;
    }

    // Call embedded init
    kprint("Calling embedded init\n", 0x00FF0000);
    __asm__("sti");  // Enable interrupts for init
    main(fb_puts);
    kprintf("Embedded init returned unexpectedly\n", 0x00FF0000);
    kernel_panic_shell("Embedded init returned");

    while(1) { __asm__("hlt"); }
}