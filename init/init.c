/* Simple init shell
 * This file is compiled and embedded into the kernel as the fallback `init`.
 * It implements a tiny line-based shell using kernel-provided input/output
 * symbols when available; when built as a standalone ELF (loaded by the
 * kernel) it falls back to using the provided print function for output and
 * disables input-dependent features.
 */

#include <stdint.h>

/* Weak kernel helpers (may be missing when building a standalone ELF). */
extern void beep(uint32_t freq, uint64_t duration) __attribute__((weak));
extern int getchar(void) __attribute__((weak));
extern int putchar(int c) __attribute__((weak));
extern void puts(const char* s) __attribute__((weak));
/* Embedded utility hooks (provided when init is linked into kernel) */
extern void util_ls(const char *path) __attribute__((weak));
extern void util_cat(const char *path) __attribute__((weak));
extern void fb_backspace(void) __attribute__((weak));
extern void fb_cursor_show(void) __attribute__((weak));
extern void fb_cursor_hide(void) __attribute__((weak));
extern int try_getchar(void) __attribute__((weak));
extern void kernel_panic_shell(const char *reason) __attribute__((weak));

static void (*g_print_fn)(const char*) = 0;

void out_puts(const char *s) {
    // Hide cursor while printing to avoid the inverted block overlapping text
    if (fb_cursor_hide) fb_cursor_hide();
    if (puts) puts(s);
    else if (g_print_fn) g_print_fn(s);
    if (fb_cursor_show) fb_cursor_show();
}

void out_putchar(char c) {
    if (fb_cursor_hide) fb_cursor_hide();
    if (putchar) putchar((int)c);
    else if (g_print_fn) {
        char tmp[2] = { c, '\0' };
        g_print_fn(tmp);
    }
    if (fb_cursor_show) fb_cursor_show();
}

int in_getchar(void) {
    if (getchar) return getchar();
    return -1;
}

static int my_strlen(const char *s) { int i = 0; while (s && s[i]) i++; return i; }
static int my_strcmp(const char *a, const char *b) { if (!a||!b) return (a==b)?0:(a?1:-1); int i=0; while(a[i]&&b[i]){ if(a[i]!=b[i]) return (int)(a[i]-b[i]); i++; } return (int)(a[i]-b[i]); }
static int my_strncmp(const char *a, const char *b, int n) { if(!a||!b) return (a==b)?0:(a?1:-1); for(int i=0;i<n;i++){ if(a[i]=='\0'&&b[i]=='\0') return 0; if(a[i]!=b[i]) return (int)(a[i]-b[i]); if(a[i]=='\0'||b[i]=='\0') return (int)(a[i]-b[i]); } return 0; }

void main(void (*print_fn)(const char*)) {
    g_print_fn = print_fn;

    if (beep) {
        beep(1000, 5000000000ULL);
    } else if (g_print_fn) {
        g_print_fn("[init] beep unavailable; continuing\n");
    }

    if (g_print_fn) g_print_fn("[init] starting\n");

    if (!getchar) {
        out_puts("Rhoudveine init (no input available).\n");
        out_puts("Type help when run as embedded kernel init.\n");
        for (;;) { __asm__("cli; hlt"); }
    }

    out_puts("Rhoudveine init shell. Type 'help' for commands.\n");

    const int BUF_SIZE = 128;
    char buf[BUF_SIZE];
    int pos = 0;

    for (;;) {
        if (fb_cursor_hide) fb_cursor_hide();
        out_puts("init> ");
        pos = 0;
            int blink_counter = 0;
            int cursor_state = 0;
            if (fb_cursor_show) fb_cursor_show();
            while (1) {
            int c = -1;
            if (try_getchar) c = try_getchar();
            else c = in_getchar();
            if (c <= 0) {
                // No input: handle blinking cursor using simple busy-wait
                blink_counter++;
                if (blink_counter >= 50) {
                    blink_counter = 0;
                    cursor_state = !cursor_state;
                    if (cursor_state) { if (fb_cursor_show) fb_cursor_show(); }
                    else { if (fb_cursor_hide) fb_cursor_hide(); }
                }
                // small pause
                for (volatile int z = 0; z < 20000; z++);
                continue;
            }
            if (c == '\r' || c == '\n') {
                if (fb_cursor_hide) fb_cursor_hide();
                out_putchar('\n');
                if (fb_cursor_show) fb_cursor_show();
                buf[pos] = '\0';
                break;
            }
            if (c == '\b' || c == 127) {
                if (pos > 0) {
                    pos--;
                    // Prefer kernel-provided backspace handling when available
                    if (fb_cursor_hide) fb_cursor_hide();
                    if (fb_backspace) fb_backspace();
                    else out_putchar('\b');
                    if (fb_cursor_show) fb_cursor_show();
                }
                continue;
            }
            if (pos < BUF_SIZE - 1) {
                buf[pos++] = (char)c;
                if (fb_cursor_hide) fb_cursor_hide();
                out_putchar((char)c);
                if (fb_cursor_show) fb_cursor_show();
            }
        }

        if (pos == 0) continue;

        if (my_strcmp(buf, "help") == 0) {
            out_puts("Available commands:\n");
            out_puts("  help    - show this message\n");
            out_puts("  echo ...- echo text\n");
            out_puts("  reboot  - halt the machine (no ACPI)\n");
            continue;
        }

        if (my_strcmp(buf, "reboot") == 0) {
            out_puts("Rebooting (halt)...\n");
            for (;;) { __asm__("cli; hlt"); }
        }

        if (my_strcmp(buf, "panic") == 0) {
            if (kernel_panic_shell) {
                kernel_panic_shell("manual panic from init shell");
            } else {
                out_puts("panic: kernel panic handler unavailable\n");
            }
            continue;
        }

        if (my_strncmp(buf, "panic ", 6) == 0) {
            const char *p = buf + 6;
            if (kernel_panic_shell) kernel_panic_shell(p);
            else out_puts("panic: kernel panic handler unavailable\n");
            continue;
        }

        if (my_strncmp(buf, "echo ", 5) == 0) {
            const char *p = buf + 5;
            out_puts(p);
            out_putchar('\n');
            continue;
        }

        if (my_strcmp(buf, "ls") == 0) {
            if (util_ls) {
                util_ls("/");
            } else {
                out_puts("ls: command not found\n");
            }
            continue;
        }

        if (my_strncmp(buf, "ls ", 3) == 0) {
            const char *p = buf + 3;
            if (util_ls) util_ls(p);
            else out_puts("ls: command not found\n");
            continue;
        }

        if (my_strncmp(buf, "cat ", 4) == 0) {
            const char *p = buf + 4;
            if (util_cat) util_cat(p);
            else out_puts("cat: command not found\n");
            continue;
        }

        out_puts("Unknown command. Type 'help' for list.\n");
    }
}
