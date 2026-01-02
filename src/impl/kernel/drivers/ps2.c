#include <stdint.h>
#include "include/ps2.h"
#include "include/console.h"
#include <stdint.h>

// Simple ring buffer for keyboard input (shared between PS/2 and USB)
static volatile unsigned int in_head = 0;
static volatile unsigned int in_tail = 0;
static volatile char in_buf[256];

// Add a character to the input buffer (callable from USB HID driver)
void input_add_char(char c) {
    unsigned int next = (in_head + 1) & 255;
    if (next != in_tail) {
        in_buf[in_head] = c;
        in_head = next;
    }
}

// Port addresses
#define KBD_DATA_PORT 0x60
#define KBD_STATUS_PORT 0x64

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a" (val), "Nd" (port));
}

// Minimal scancode set 1 -> ASCII map for common keys
static const char scancode_map[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b', '\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'', '`', 0,
    '\\','z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ',
};

// Shifted character map (scancode set 1) for number row and punctuation
static const char scancode_shift_map[128] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0, 0,
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0,
    '|','Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' ',
};

void ps2_handle_interrupt(void) {
    uint8_t status = inb(KBD_STATUS_PORT);
    if (!(status & 1)) return; // no data
    uint8_t sc = inb(KBD_DATA_PORT);

    // Track modifier and lock key state
    static volatile int shift = 0;
    static volatile int caps = 0;
    static volatile int numlock = 0;
    static volatile int leds_state = 0; // bit0 scroll, bit1 num, bit2 caps
    // Extended prefix state
    static volatile int extended = 0;

    // Handle special bytes: ACK (0xFA) from keyboard controller when we
    // send LED commands — ignore these so they don't pollute the input buffer.
    if (sc == 0xFA || sc == 0xFE) return;

    // Handle break codes (key release)
    if (sc & 0x80) {
        uint8_t code = sc & 0x7F;
        if (code == 0x2A || code == 0x36) { // left or right shift released
            shift = 0;
        }
        // release of extended keys resets extended flag
        extended = 0;
        return;
    }

    // Make codes (key press)
    // Handle modifier/lock keys explicitly
    if (sc == 0xE0) { extended = 1; return; }
    if (sc == 0x2A || sc == 0x36) { // shift press
        shift = 1; return;
    }
    if (sc == 0x3A) { // Caps Lock
        caps = !caps;
        // update LEDs
        leds_state = (leds_state & ~0x04) | (caps ? 0x04 : 0);
        // Send LED update (keyboard command 0xED)
        // Wait for input buffer to be clear
        for (int i = 0; i < 100000; i++) { uint8_t st = inb(KBD_STATUS_PORT); if (!(st & 2)) break; }
        outb(KBD_DATA_PORT, 0xED);
        // write LEDs mask
        for (int i = 0; i < 100000; i++) { uint8_t st = inb(KBD_STATUS_PORT); if (!(st & 2)) break; }
        outb(KBD_DATA_PORT, (uint8_t)leds_state);
        // show status
        if (caps) fb_puts("CAPS ON\n"); else fb_puts("CAPS OFF\n");
        return;
    }
    if (sc == 0x77) { // Num Lock
        numlock = !numlock;
        leds_state = (leds_state & ~0x02) | (numlock ? 0x02 : 0);
        for (int i = 0; i < 100000; i++) { uint8_t st = inb(KBD_STATUS_PORT); if (!(st & 2)) break; }
        outb(KBD_DATA_PORT, 0xED);
        for (int i = 0; i < 100000; i++) { uint8_t st = inb(KBD_STATUS_PORT); if (!(st & 2)) break; }
        outb(KBD_DATA_PORT, (uint8_t)leds_state);
        if (numlock) fb_puts("NUM ON\n"); else fb_puts("NUM OFF\n");
        return;
    }

        // Handle keypad numeric block (scancodes 0x47..0x53) with NumLock
        if (sc >= 0x47 && sc <= 0x53) {
            static const char keypad_num_map[] = {'7','8','9','-','4','5','6','+','1','2','3','0','.'};
            char out = 0;
            if (numlock) out = keypad_num_map[sc - 0x47];
            // if numlock is off, we currently don't translate keypad to navigation
            // sequences (could be added later). If out is zero, treat as no character.
            if (out) {
                unsigned int next = (in_head + 1) & 255;
                if (next != in_tail) {
                    in_buf[in_head] = out;
                    in_head = next;
                }
            }
            return;
        }

        if (sc < sizeof(scancode_map)) {
        char c = scancode_map[sc];
        char sc_shift = scancode_shift_map[sc];
        if (c) {
            char out = c;
            // If we have a shifted mapping for this scancode and shift is down,
            // prefer it.
            if (shift && sc_shift) out = sc_shift;
            else {
                // Apply Caps Lock for letters (toggle case when shift not pressed)
                if ((c >= 'a' && c <= 'z')) {
                    if (shift ^ caps) out = (char)(c - ('a' - 'A'));
                }
                // For number-row, if shift pressed and no explicit shift map,
                // we leave the base char; shift map above covers standard cases.
            }

            // Enqueue
            unsigned int next = (in_head + 1) & 255;
            if (next != in_tail) {
                in_buf[in_head] = out;
                in_head = next;
            }
        }
    }
}

// USB keyboard polling function (from xhci.c)
extern void usb_kbd_poll(void);

int ps2_getchar(void) {
    // Active polling loop for both PS/2 and USB keyboard
    // We poll USB then briefly enable interrupts for PS/2
    while (in_head == in_tail) {
        // Poll USB keyboard (non-blocking)
        usb_kbd_poll();
        
        // Check PS/2 keyboard (non-blocking) 
        uint8_t status = inb(KBD_STATUS_PORT);
        if (status & 1) {
            // Data available, handle it
            ps2_handle_interrupt();
        }
        
        // Small delay to avoid burning CPU 100%
        for (volatile int i = 0; i < 1000; i++);
    }
    char c = in_buf[in_tail];
    in_tail = (in_tail + 1) & 255;
    return (int)c;
}

// Non-blocking try_getchar: return -1 when no character is available
int try_getchar(void) {
    // Poll USB keyboard first
    usb_kbd_poll();
    
    // Check PS/2 keyboard for data
    uint8_t status = inb(KBD_STATUS_PORT);
    if (status & 1) {
        ps2_handle_interrupt();
    }
    
    // Check if any character is now available
    if (in_head == in_tail) return -1;
    char c = in_buf[in_tail];
    in_tail = (in_tail + 1) & 255;
    return (int)c;
}

void ps2_init(void) {
    // 1. Flush output buffer
    while (inb(KBD_STATUS_PORT) & 1) {
        inb(KBD_DATA_PORT);
    }
    
    // 2. Enable auxiliary device (mouse) interaction if needed (skip for now)
    
    // 3. Enable Keyboard (Command 0xAE)
    outb(KBD_STATUS_PORT, 0xAE);
    
    // 4. Reset Keyboard (0xFF) -> Expect 0xFA (ACK), 0xAA (Success)
    // Some laptops hate reset, so maybe just Enable Scanning (0xF4)
    
    // Send Enable Scanning (0xF4)
    // Wait for buffer empty
    for (int i=0; i<10000; i++) { if (!(inb(KBD_STATUS_PORT) & 2)) break; }
    outb(KBD_DATA_PORT, 0xF4);
    
    // Wait for ACK
    uint8_t ack = 0;
    for (int i=0; i<100000; i++) {
        if (inb(KBD_STATUS_PORT) & 1) {
            ack = inb(KBD_DATA_PORT);
            break;
        }
    }
    
    if (ack == 0xFA) {
        // Success
    }
}
