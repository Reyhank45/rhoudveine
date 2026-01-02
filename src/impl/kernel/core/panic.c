#include <stdint.h>
#include <stddef.h>
#include "include/panic.h"

/* External Beep Function to make sound */
extern void beep(double duration_ms, double frequency_hz, bool stop);
/* External printing helpers (defined in main.c). */
extern void fb_puts(const char *s);
extern void fb_putc(char c);
extern void kprintf(const char *format, uint32_t color, ...);
// Input helpers (ps2)
extern int try_getchar(void) __attribute__((weak));
extern int ps2_getchar(void) __attribute__((weak));


// Simple helper to print a hex 64-bit value with label
// Register snapshot structure (to hold panic-time registers)
struct regs_snapshot {
    uint64_t rip;
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
};

static struct regs_snapshot saved_regs;
static int saved_regs_valid = 0;

// Capture registers into the provided snapshot (reads current CPU state)
static void capture_regs(struct regs_snapshot *s) {
    __asm__ volatile ("mov %%rax, %0" : "=r" (s->rax));
    __asm__ volatile ("mov %%rbx, %0" : "=r" (s->rbx));
    __asm__ volatile ("mov %%rcx, %0" : "=r" (s->rcx));
    __asm__ volatile ("mov %%rdx, %0" : "=r" (s->rdx));
    __asm__ volatile ("mov %%rsi, %0" : "=r" (s->rsi));
    __asm__ volatile ("mov %%rdi, %0" : "=r" (s->rdi));
    __asm__ volatile ("mov %%rbp, %0" : "=r" (s->rbp));
    __asm__ volatile ("mov %%rsp, %0" : "=r" (s->rsp));
    __asm__ volatile ("mov %%r8, %0" : "=r" (s->r8));
    __asm__ volatile ("mov %%r9, %0" : "=r" (s->r9));
    __asm__ volatile ("mov %%r10, %0" : "=r" (s->r10));
    __asm__ volatile ("mov %%r11, %0" : "=r" (s->r11));
    __asm__ volatile ("mov %%r12, %0" : "=r" (s->r12));
    __asm__ volatile ("mov %%r13, %0" : "=r" (s->r13));
    __asm__ volatile ("mov %%r14, %0" : "=r" (s->r14));
    __asm__ volatile ("mov %%r15, %0" : "=r" (s->r15));
    __asm__ volatile ("call 1f\n1: pop %0" : "=r" (s->rip));
}

// Dump registers contained in the snapshot
static void dump_regs_from(const struct regs_snapshot *s) {
    fb_puts("\nRegister state:\n");
    kprintf("RIP: %lx\n", 0xFFFFFFFF, s->rip);
    kprintf("RSP: %lx  RBP: %lx\n", 0xFFFFFFFF, s->rsp, s->rbp);
    kprintf("RAX: %lx  RBX: %lx\n", 0xFFFFFFFF, s->rax, s->rbx);
    kprintf("RCX: %lx  RDX: %lx\n", 0xFFFFFFFF, s->rcx, s->rdx);
    kprintf("RSI: %lx  RDI: %lx\n", 0xFFFFFFFF, s->rsi, s->rdi);
    kprintf("R8 : %lx  R9 : %lx\n", 0xFFFFFFFF, s->r8, s->r9);
    kprintf("R10: %lx  R11: %lx\n", 0xFFFFFFFF, s->r10, s->r11);
    kprintf("R12: %lx  R13: %lx\n", 0xFFFFFFFF, s->r12, s->r13);
    kprintf("R14: %lx  R15: %lx\n", 0xFFFFFFFF, s->r14, s->r15);
}

// Dump current live registers (reads registers at time of call)
static void dump_regs(void) {
    struct regs_snapshot temp;
    capture_regs(&temp);
    dump_regs_from(&temp);
}

// Dump some memory at `addr` for `words` 8-byte words
static void dump_stack_region(uint64_t *addr, size_t words) {
    for (size_t i = 0; i < words; i += 2) {
        uint64_t a = (uint64_t)(uintptr_t)(addr + i);
        uint64_t v1 = 0, v2 = 0;
        // Try to read memory (may fault in exotic cases, but on panic assume OK)
        v1 = addr[i];
        if (i + 1 < words) v2 = addr[i + 1];
        kprintf("%lx: %lx %lx\n", 0xFFFFFFFF, a, v1, v2);
    }
}

// Read a single input character blocking (prefer ps2_getchar, fall back to try_getchar busy-wait)
static int panic_getchar_blocking(void) {
    // Poll-only version: only use try_getchar to avoid any HLT-based
    // blocking paths that may make the system appear halted.
    if (try_getchar) {
        int c = -1;
        while ((c = try_getchar()) == -1) { /* busy-wait */ }
        return c;
    }
    // If try_getchar isn't present, fall back to returning -1 so the
    // caller can continue without invoking HLT-based readers.
    return -1;
}

void kernel_panic_shell(const char *reason) {
    // take a snapshot with interrupts disabled
    __asm__("cli");

    fb_puts("\n*** KERNEL PANIC - entering panic shell ***\n");
    if (reason) {
        fb_puts("Reason: ");
        fb_puts(reason);
        fb_puts("\n");
    }
    // Warn the user that there is something wrong
    beep(1, 500, false);

    // initial dumps while interrupts are off
    capture_regs(&saved_regs);
    saved_regs_valid = 1;
    dump_regs_from(&saved_regs);
    uint64_t rsp;
    __asm__ volatile ("mov %%rsp, %0" : "=r" (rsp));
    fb_puts("\nStack dump (RSP onwards):\n");
    dump_stack_region((uint64_t*)(uintptr_t)rsp, 16);

    fb_puts("\nEntering interactive panic shell. Type 'help' for commands.\n");

    // enable interrupts so keyboard IRQs can fill the input buffer
    __asm__("sti");

    // Interactive loop: read a line and handle a few commands
    for (;;) {
        fb_puts("panic> ");
        char line[128];
        int pos = 0;
        while (1) {
            int c = panic_getchar_blocking();
            if (c < 0) { /* no input available */ continue; }
            if (c == '\r' || c == '\n') {
                fb_putc('\n');
                line[pos] = '\0';
                break;
            }
            if ((c == '\b' || c == 127) && pos > 0) {
                pos--;
                // backspace visually
                fb_putc('\b'); fb_putc(' '); fb_putc('\b');
                continue;
            }
            if (pos < (int)sizeof(line) - 1) {
                line[pos++] = (char)c;
                fb_putc((char)c);
            }
        }

        if (pos == 0) continue;

        if (line[0] == '\0') continue;
        if (/* strcmp(line, "help") == 0 */ 0 == 0) {
            if (line[0] == 'h' && line[1] == 'e' && line[2] == 'l' && line[3] == 'p' && line[4] == '\0') {
                fb_puts("Available commands:\n");
                fb_puts("  help    - show this message\n");
                fb_puts("  regs    - live registers (current)\n");
                fb_puts("  panicregs - registers captured at panic entry\n");
                fb_puts("  stack   - dump stack (RSP)\n");
                fb_puts("  halt    - halt the machine\n");
                continue;
            }
        }

        if (line[0] == 'r' && line[1] == 'e' && line[2] == 'g' && line[3] == 's' && line[4] == '\0') {
            // live registers
            dump_regs();
            continue;
        }

        if (line[0] == 'p' && line[1] == 'a' && line[2] == 'n' && line[3] == 'i' && line[4] == 'c' && line[5] == 'r' && line[6] == 'e' && line[7] == 'g' && line[8] == 's' && line[9] == '\0') {
            if (saved_regs_valid) dump_regs_from(&saved_regs);
            else fb_puts("No saved panic registers available\n");
            continue;
        }

        if (line[0] == 's' && line[1] == 't' && line[2] == 'a' && line[3] == 'c' && line[4] == 'k' && line[5] == '\0') {
            uint64_t rsp_now;
            __asm__ volatile ("mov %%rsp, %0" : "=r" (rsp_now));
            fb_puts("Stack dump (RSP):\n");
            dump_stack_region((uint64_t*)(uintptr_t)rsp_now, 32);
            continue;
        }

        if (line[0] == 'h' && line[1] == 'a' && line[2] == 'l' && line[3] == 't' && line[4] == '\0') {
            fb_puts("Halting...\n");
            for (;;) { __asm__("cli; hlt"); }
        }

        fb_puts("Unknown command. Type 'help' for list.\n");
    }
}
