/* Simple init shell
 * This file is compiled and embedded into the kernel as the fallback `init`.
 * It implements a tiny line-based shell using kernel-provided input/output
 * symbols when available; when built as a standalone ELF (loaded by the
 * kernel) it falls back to using the provided print function for output and
 * disables input-dependent features.
 */

#include <stdint.h>
#include <stddef.h>

/* Weak kernel helpers (may be missing when building a standalone ELF). */
extern void beep(uint32_t freq, uint64_t duration) __attribute__((weak));
extern int getchar(void) __attribute__((weak));
extern int putchar(int c) __attribute__((weak));
extern void puts(const char* s) __attribute__((weak));
/* Embedded utility hooks (provided when init is linked into kernel) */
extern void util_ls(const char *path) __attribute__((weak));
extern void util_cat(const char *path) __attribute__((weak));

/* Current working directory (for display purposes) */
static char g_cwd[128] = "/";

/* ACPI power management */
extern void acpi_shutdown(void) __attribute__((weak));
extern void acpi_reboot(void) __attribute__((weak));

/* Timer functions */
extern uint64_t timer_get_uptime_ms(void) __attribute__((weak));
extern uint64_t timer_get_ticks(void) __attribute__((weak));

/* AHCI disk functions */
extern int ahci_read_sectors(uint64_t lba, uint32_t count, uint8_t *buffer) __attribute__((weak));
extern int ahci_is_initialized(void) __attribute__((weak));

/* VFS functions */
extern int vfs_open(const char *path, uint32_t flags) __attribute__((weak));
extern int vfs_close(int fd) __attribute__((weak));
extern int vfs_read(int fd, void *buffer, size_t count) __attribute__((weak));
extern int vfs_write(int fd, const void *buffer, size_t count) __attribute__((weak));
extern int vfs_mkdir(const char *path) __attribute__((weak));

// VFS directory entry
struct dirent {
    uint32_t inode;
    char name[256];
    uint8_t type;
};
extern int vfs_readdir(int fd, struct dirent *entry) __attribute__((weak));

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
            out_puts("  help      - show this message\n");
            out_puts("  echo ...  - echo text\n");
            out_puts("  cdl [path]- list directory contents\n");
            out_puts("  dump <f>  - display file contents\n");
            out_puts("  write <file> <text> - write text to file\n");
            out_puts("  mkdir <dir> - create directory\n");
            out_puts("  cd <path> - change directory\n");
            out_puts("  uptime    - show system uptime\n");
            out_puts("  diskread <lba> - read sector from disk\n");
            out_puts("  shutdown  - ACPI shutdown\n");
            out_puts("  reboot    - ACPI reboot\n");
            continue;
        }

        if (my_strcmp(buf, "uptime") == 0) {
            if (timer_get_uptime_ms) {
                uint64_t ms = timer_get_uptime_ms();
                uint64_t seconds = ms / 1000;
                uint64_t minutes = seconds / 60;
                uint64_t hours = minutes / 60;
                
                seconds %= 60;
                minutes %= 60;
                
                char hour_str[20], min_str[20], sec_str[20];
                // Simple number to string conversion
                int h_len = 0, m_len = 0, s_len = 0;
                uint64_t h = hours, m = minutes, s = seconds;
                
                if (h == 0) { hour_str[h_len++] = '0'; }
                else { while (h > 0) { hour_str[h_len++] = '0' + (h % 10); h /= 10; } }
                hour_str[h_len] = '\0';
                // Reverse
                for (int i = 0; i < h_len/2; i++) {
                    char tmp = hour_str[i];
                    hour_str[i] = hour_str[h_len-1-i];
                    hour_str[h_len-1-i] = tmp;
                }
                
                if (m == 0) { min_str[m_len++] = '0'; }
                else { while (m > 0) { min_str[m_len++] = '0' + (m % 10); m /= 10; } }
                min_str[m_len] = '\0';
                for (int i = 0; i < m_len/2; i++) {
                    char tmp = min_str[i];
                    min_str[i] = min_str[m_len-1-i];
                    min_str[m_len-1-i] = tmp;
                }
                
                if (s == 0) { sec_str[s_len++] = '0'; }
                else { while (s > 0) { sec_str[s_len++] = '0' + (s % 10); s /= 10; } }
                sec_str[s_len] = '\0';
                for (int i = 0; i < s_len/2; i++) {
                    char tmp = sec_str[i];
                    sec_str[i] = sec_str[s_len-1-i];
                    sec_str[s_len-1-i] = tmp;
                }
                
                out_puts("Uptime: ");
                out_puts(hour_str);
                out_puts("h ");
                out_puts(min_str);
                out_puts("m ");
                out_puts(sec_str);
                out_puts("s\n");
            } else {
                out_puts("Timer not available\n");
            }
            continue;
        }

        if (my_strcmp(buf, "shutdown") == 0) {
            if (acpi_shutdown) {
                out_puts("Initiating ACPI shutdown...\n");
                acpi_shutdown();
            } else {
                out_puts("ACPI shutdown not available, halting\n");
                for (;;) { __asm__("cli; hlt"); }
            }

        // VFS cdl command (list directory)
        if (my_strcmp(buf, "cdl") == 0 || my_strncmp(buf, "cdl ", 4) == 0) {
            if (!vfs_open || !vfs_readdir || !vfs_close) {
                out_puts("VFS not available\n");
                continue;
            }
            
            const char *path = (buf[3] == ' ') ? buf + 4 : "/";
            int fd = vfs_open(path, 0);
            if (fd < 0) {
                out_puts("Failed to open directory\n");
                continue;
            }
            
            struct dirent entry;
            while (vfs_readdir(fd, &entry) == 0) {
                out_puts(entry.name);
                if (entry.type & 0x02) out_puts("/");  // Directory
                out_puts("\n");
            }
            
            vfs_close(fd);
            continue;
        }

        // VFS dump command (display file)
        if (my_strncmp(buf, "dump ", 5) == 0) {
            if (!vfs_open || !vfs_read || !vfs_close) {
                out_puts("VFS not available\n");
                continue;
            }
            
            const char *path = buf + 5;
            int fd = vfs_open(path, 0);
            if (fd < 0) {
                out_puts("Failed to open file\n");
                continue;
            }
            
            static uint8_t read_buf[512];
            int bytes;
            while ((bytes = vfs_read(fd, read_buf, sizeof(read_buf))) > 0) {
                for (int i = 0; i < bytes; i++) {
                    out_putchar(read_buf[i]);
                }
            }
            out_putchar('\n');
            
            vfs_close(fd);
            continue;
        }

        // VFS write command
        if (my_strncmp(buf, "write ", 6) == 0) {
            if (!vfs_open || !vfs_write || !vfs_close) {
                out_puts("VFS not available\n");
                continue;
            }
            
            // Parse: write <filename> <text>
            const char *p = buf + 6;
            char filename[128];
            int i = 0;
            
            // Extract filename
            while (*p && *p != ' ' && i < 127) {
                filename[i++] = *p++;
            }
            filename[i] = '\0';
            
            if (*p == ' ') p++; // Skip space
            
            // Open file for writing (O_CREAT | O_WRONLY)
            int fd = vfs_open(filename, 0x0101);
            if (fd < 0) {
                out_puts("Failed to open file for writing\n");
                continue;
            }
            
            int len = my_strlen(p);
            int written = vfs_write(fd, p, len);
            vfs_write(fd, "\n", 1);  // Add newline
            vfs_close(fd);
            
            out_puts("Wrote ");
            // Print number
            char num_str[16];
            int num_len = 0;
            int temp = written + 1;
            if (temp == 0) { num_str[num_len++] = '0'; }
            else {
                while (temp > 0) {
                    num_str[num_len++] = '0' + (temp % 10);
                    temp /= 10;
                }
            }
            // Reverse
            for (i = 0; i < num_len/2; i++) {
                char tmp = num_str[i];
                num_str[i] = num_str[num_len-1-i];
                num_str[num_len-1-i] = tmp;
            }
            num_str[num_len] = '\0';
            out_puts(num_str);
            out_puts(" bytes\n");
            continue;
        }

        // VFS mkdir command
        if (my_strncmp(buf, "mkdir ", 6) == 0) {
            if (!vfs_mkdir) {
                out_puts("VFS not available\n");
                continue;
            }
            
            const char *path = buf + 6;
            if (vfs_mkdir(path) == 0) {
                out_puts("Directory created\n");
            } else {
                out_puts("Failed to create directory\n");
            }
            continue;
        }

        if (my_strncmp(buf, "diskread ", 9) == 0) {
            if (ahci_is_initialized && ahci_read_sectors) {
                if (!ahci_is_initialized()) {
                    out_puts("AHCI not initialized\n");
                    continue;
                }
                
                // Parse LBA (simple atoi)
                const char *p = buf + 9;
                uint64_t lba = 0;
                while (*p >= '0' && *p <= '9') {
                    lba = lba * 10 + (*p - '0');
                    p++;
                }
                
                // Allocate buffer (512 bytes)
                static uint8_t sector_buf[512];
                
                out_puts("Reading sector ");
                // Print LBA
                char lba_str[32];
                int len = 0;
                uint64_t temp = lba;
                if (temp == 0) { lba_str[len++] = '0'; }
                else {
                    while (temp > 0) {
                        lba_str[len++] = '0' + (temp % 10);
                        temp /= 10;
                    }
                }
                lba_str[len] = '\0';
                // Reverse
                for (int i = 0; i < len/2; i++) {
                    char tmp = lba_str[i];
                    lba_str[i] = lba_str[len-1-i];
                    lba_str[len-1-i] = tmp;
                }
                out_puts(lba_str);
                out_puts("...\n");
                
                int result = ahci_read_sectors(lba, 1, sector_buf);
                if (result == 0) {
                    out_puts("Read successful! First 64 bytes:\n");
                    // Hex dump first 64 bytes
                    for (int i = 0; i < 64; i++) {
                        if (i % 16 == 0) {
                            out_putchar('\n');
                        }
                        uint8_t byte = sector_buf[i];
                        // Convert to hex
                        char hex[3];
                        hex[0] = (byte >> 4) < 10 ? '0' + (byte >> 4) : 'A' + (byte >> 4) - 10;
                        hex[1] = (byte & 0xF) < 10 ? '0' + (byte & 0xF) : 'A' + (byte & 0xF) - 10;
                        hex[2] = '\0';
                        out_puts(hex);
                        out_putchar(' ');
                    }
                    out_putchar('\n');
                } else {
                    out_puts("Read failed!\n");
                }
            } else {
                out_puts("AHCI driver not available\n");
            }
            continue;
        }
        }

        if (my_strcmp(buf, "reboot") == 0) {
            if (acpi_reboot) {
                out_puts("Initiating ACPI reboot...\n");
                acpi_reboot();
            } else {
                out_puts("ACPI reboot not available, halting\n");
                for (;;) { __asm__("cli; hlt"); }
            }
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



        // cdl command (list directory)
        if (my_strcmp(buf, "cdl") == 0 || my_strncmp(buf, "cdl ", 4) == 0) {
            if (!vfs_open || !vfs_readdir || !vfs_close) {
                out_puts("VFS not available\n");
                continue;
            }
            
            const char *path = (buf[3] == ' ') ? buf + 4 : "/";
            int fd = vfs_open(path, 0);
            if (fd < 0) {
                out_puts("Failed to open directory\n");
                continue;
            }
            
            struct dirent entry;
            while (vfs_readdir(fd, &entry) == 0) {
                out_puts(entry.name);
                if (entry.type & 0x02) out_puts("/");  // Directory
                out_puts("\n");
            }
            
            vfs_close(fd);
            continue;
        }

        // dump command (display file)
        if (my_strncmp(buf, "dump ", 5) == 0) {
            if (!vfs_open || !vfs_read || !vfs_close) {
                out_puts("VFS not available\n");
                continue;
            }
            
            const char *path = buf + 5;
            int fd = vfs_open(path, 0);
            if (fd < 0) {
                out_puts("Failed to open file\n");
                continue;
            }
            
            static uint8_t read_buf[512];
            int bytes;
            while ((bytes = vfs_read(fd, read_buf, sizeof(read_buf))) > 0) {
                for (int i = 0; i < bytes; i++) {
                    out_putchar(read_buf[i]);
                }
            }
            out_putchar('\n');
            
            vfs_close(fd);
            continue;
        }

        // cd command (change directory)
        if (my_strncmp(buf, "cd ", 3) == 0) {
            const char *p = buf + 3;
            // Simple cd: just update g_cwd
            int len = my_strlen(p);
            if (len > 0 && len < (int)sizeof(g_cwd) - 1) {
                for (int i = 0; i < len; i++) g_cwd[i] = p[i];
                g_cwd[len] = '\0';
                out_puts("Changed directory to: ");
                out_puts(g_cwd);
                out_putchar('\n');
            } else {
                out_puts("cd: invalid path\n");
            }
            continue;
        }

        // Auto-cd: if input starts with '/', treat as path and cd to it
        if (buf[0] == '/') {
            int len = my_strlen(buf);
            if (len > 0 && len < (int)sizeof(g_cwd) - 1) {
                for (int i = 0; i < len; i++) g_cwd[i] = buf[i];
                g_cwd[len] = '\0';
                out_puts("Changed directory to: ");
                out_puts(g_cwd);
                out_putchar('\n');
                continue;
            }
        }

        out_puts("Unknown command. Type 'help' for list.\n");
    }
}
