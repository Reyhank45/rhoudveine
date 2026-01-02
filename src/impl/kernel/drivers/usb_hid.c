#include "include/autoconf.h"
#include "include/nvnode.h"
#include <stdint.h>
#include <stddef.h>

extern void kprintf(const char *format, uint32_t color, ...);
extern void fb_putc(char c);  // Framebuffer putchar for keyboard input

// USB HID report descriptor item types
#define HID_ITEM_INPUT      0x80
#define HID_ITEM_OUTPUT     0x90
#define HID_ITEM_FEATURE    0xB0

// Standard HID Boot Protocol Keyboard Report
typedef struct {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keycodes[6];
} __attribute__((packed)) hid_kbd_report_t;

// Keyboard state
static uint8_t kbd_modifiers = 0;
static uint8_t prev_keycodes[6] = {0};

// HID keycode to ASCII conversion table (US layout)
static const char hid_to_ascii[128] = {
    0, 0, 0, 0,                  // 0x00-0x03 (reserved)
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', // 0x04-0x0D
    'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', // 0x0E-0x17
    'u', 'v', 'w', 'x', 'y', 'z',                     // 0x18-0x1D
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', // 0x1E-0x27
    '\n', 0x1B, '\b', '\t', ' ',  // Enter, Escape, Backspace, Tab, Space
    '-', '=', '[', ']', '\\',    // 0x2D-0x31
    '#', ';', '\'', '`', ',', '.', '/', // 0x32-0x38
    0, // Caps Lock
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // F1-F12
    // ... rest is 0
};

static const char hid_to_ascii_shift[128] = {
    0, 0, 0, 0,
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
    'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
    'U', 'V', 'W', 'X', 'Y', 'Z',
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '\n', 0x1B, '\b', '\t', ' ',
    '_', '+', '{', '}', '|',
    '~', ':', '"', '~', '<', '>', '?',
};

// Add character to system input buffer (from PS/2 driver)
extern void input_add_char(char c);

// Process a keyboard report
void usb_kbd_process_report(hid_kbd_report_t *report) {
    #ifdef CONFIG_USB_KBD
    kbd_modifiers = report->modifiers;
    int shift = (kbd_modifiers & 0x22); // Left or Right Shift
    
    for (int i = 0; i < 6; i++) {
        uint8_t keycode = report->keycodes[i];
        if (keycode == 0) continue;
        
        // Check if this is a new keypress
        int is_new = 1;
        for (int j = 0; j < 6; j++) {
            if (prev_keycodes[j] == keycode) {
                is_new = 0;
                break;
            }
        }
        
        if (is_new && keycode < 128) {
            char c = shift ? hid_to_ascii_shift[keycode] : hid_to_ascii[keycode];
            if (c) {
                // Add to system input buffer (init shell will echo)
                input_add_char(c);
            }
        }
    }
    
    // Update previous state
    for (int i = 0; i < 6; i++) {
        prev_keycodes[i] = report->keycodes[i];
    }
    #endif
}

// Initialize USB HID subsystem
void usb_hid_init(void) {
    #ifdef CONFIG_USB_HID
    kprintf("USB_HID: Initializing USB HID class driver...\n", 0x00FF0000);
    
    #ifdef CONFIG_USB_KBD
    kprintf("USB_HID: USB Keyboard driver enabled\n", 0x00FF0000);
    #endif
    
    kprintf("USB_HID: Initialization complete\n", 0x00FF0000);
    #endif
}

// Register a USB keyboard device (called from xHCI when device is detected)
void usb_kbd_register(uint16_t vendor_id, uint16_t product_id) {
    #ifdef CONFIG_USB_KBD
    kprintf("USB_KBD: Registered keyboard (VID=%x, PID=%x)\n", 0x00FFFF00, vendor_id, product_id);
    
    // Register with NVNODE
    #ifdef CONFIG_NVNODE
    nvnode_add_usb_device(vendor_id, product_id);
    #endif
    #endif
}
