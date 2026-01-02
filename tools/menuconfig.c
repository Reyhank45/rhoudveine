#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>

// --- Kconfig Parsing ---
typedef struct {
    char name[64];
    char prompt[128];
    int value; // 0 or 1
    char help[1024];
    char dependency[64]; // Name of config this depends on
} ConfigItem;

#define MAX_ITEMS 64
ConfigItem items[MAX_ITEMS];
int item_count = 0;

void parse_kconfig(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return;
    
    char line[1024];
    ConfigItem *curr = NULL;
    int help_mode = 0;
    
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p && isspace(*p)) p++;
        if (*p == '\0' || *p == '#') continue;
        
        if (strncmp(p, "config ", 7) == 0) {
            if (item_count < MAX_ITEMS) {
                curr = &items[item_count++];
                sscanf(p + 7, "%s", curr->name);
                curr->value = 0; // default n
                curr->prompt[0] = '\0';
                curr->help[0] = '\0';
                curr->dependency[0] = '\0';
                help_mode = 0;
            } else {
                curr = NULL;
            }
        } else if (curr) {
            if (strncmp(p, "bool ", 5) == 0) {
                // simple prompt extraction
                char *q = strchr(p, '"');
                if (q) {
                    char *end = strchr(q + 1, '"');
                    if (end) {
                        *end = '\0';
                        strncpy(curr->prompt, q + 1, 127);
                    }
                }
            } else if (strncmp(p, "default ", 8) == 0) {
                if (p[8] == 'y') curr->value = 1;
            } else if (strncmp(p, "depends on ", 11) == 0) {
                sscanf(p + 11, "%s", curr->dependency);
            } else if (strncmp(p, "help", 4) == 0) {
                help_mode = 1;
            } else if (help_mode) {
                strncat(curr->help, p, 1023 - strlen(curr->help));
            }
        }
    }
    fclose(f);
}

void load_config(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return;
    char line[1024];
    while(fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p && isspace(*p)) p++;
        if (*p == '#' || *p == '\0') continue;
        char *eq = strchr(p, '=');
        if (eq) {
            *eq = '\0';
            char *val = eq + 1;
            // trim
            while(*val && isspace(*val)) val++;
            for(int i=0; i<item_count; i++) {
                if (strcmp(items[i].name, p) == 0) {
                    items[i].value = (val[0] == 'y');
                }
            }
        }
    }
    fclose(f);
}

void save_config(const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) return;
    fprintf(f, "# Generated config\n");
    for(int i=0; i<item_count; i++) {
        fprintf(f, "%s=%s\n", items[i].name, items[i].value ? "y" : "n");
    }
    fclose(f);
}

// Check if dependency is met
int is_visible(int index) {
    if (items[index].dependency[0] == '\0') return 1;
    
    // Find dependency
    for(int i=0; i<item_count; i++) {
        if (strcmp(items[i].name, items[index].dependency) == 0) {
            return items[i].value;
        }
    }
    return 1; // Dependency not found? Default visible
}

// Validation pass: ensure items with unmet deps are disabled
void validate_dependencies() {
    for(int i=0; i<item_count; i++) {
        if (!is_visible(i)) {
            items[i].value = 0;
        }
    }
}

// --- Terminal Control ---
struct termios orig_termios;
void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    printf("\033[?25h"); // Show cursor
}
void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    printf("\033[?25l"); // Hide cursor
}

int main(int argc, char **argv) {
    init_kconfig:
    parse_kconfig("Kconfig");
    load_config(".config");
    
    enable_raw_mode();
    
    int selected = 0;
    while(1) {
        validate_dependencies();

        // Clear screen
        printf("\033[2J\033[H");
        
        printf("\033[1;37;44m Rhoudveine Kernel Configuration \033[0m\n\n");
        
        for(int i=0; i<item_count; i++) {
            int visible = is_visible(i);
            
            if (i == selected) printf("\033[7m"); // Invert
            
            if (visible) {
                 printf(" [%c] %s ", items[i].value ? '*' : ' ', items[i].prompt);
            } else {
                 printf(" [-] %s (Depends on %s) ", items[i].prompt, items[i].dependency);
            }
            
            if (i == selected) printf("\033[0m");
            printf("\n");
        }
        
        printf("\n\033[90m [Space] Toggle  [Enter] Save & Exit  [q] Quit\033[0m\n");
        
        // Show help
        if (selected >= 0 && selected < item_count) {
            printf("\n--- Help ---\n");
            printf("%s", items[selected].help);
        }

        char c;
        read(STDIN_FILENO, &c, 1);
        
        if (c == 'q') break;
        if (c == 10) { // Enter
            save_config(".config");
            break;
        }
        if (c == ' ') {
            if (is_visible(selected)) {
                items[selected].value = !items[selected].value;
            }
        }
        if (c == 27) { // Escape sequence
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) == 0) break;
            if (read(STDIN_FILENO, &seq[1], 1) == 0) break;
            if (seq[0] == '[') {
                if (seq[1] == 'A') { // Up
                    if (selected > 0) selected--;
                } else if (seq[1] == 'B') { // Down
                    if (selected < item_count - 1) selected++;
                }
            }
        }
    }
    
    return 0;
}
