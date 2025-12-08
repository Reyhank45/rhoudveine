#include "stdio.h"
#include "console.h"

static int starts_with(const char* s, const char* p) {
    for (; *p; p++, s++) if (*s != *p) return 0;
    return 1;
}

void shell_main(void) {
    char line[128];
    int pos = 0;
    puts("simple-shell v0.1\n");
    puts("Type 'help' for commands\n");
    puts("\nshell> ");

    while (1) {
        int c = getchar();
        if (c == '\r') c = '\n';
        if (c == '\n') {
            putchar('\n');
            line[pos] = '\0';
            if (pos == 0) { puts("shell> "); continue; }

            if (starts_with(line, "help")) {
                puts("Commands: help echo info clear exit\n");
            } else if (starts_with(line, "echo ")) {
                puts(line + 5);
                putchar('\n');
            } else if (starts_with(line, "info")) {
                puts("Rhoudveine OS PRE-ALPHA\n");
            } else if (starts_with(line, "clear")) {
                // simple clear: print newlines
                for (int i = 0; i < 50; i++) putchar('\n');
            } else if (starts_with(line, "exit")) {
                puts("Bye\n");
                while (1) { __asm__ volatile ("hlt"); }
            } else {
                puts("Unknown command\n");
            }

            pos = 0;
            puts("shell> ");
            continue;
        }

        if (c == 8 || c == 127) {
            if (pos > 0) {
                pos--;
                // backspace: move back, write space, move back
                putchar('\b'); putchar(' '); putchar('\b');
            }
            continue;
        }

        if (pos < (int)sizeof(line) - 1) {
            line[pos++] = (char)c;
            putchar(c);
        }
    }
}
