#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE 1024
#define MAX_VARS 128

typedef struct {
    char key[64];
    char value[64];
} ConfigVar;

ConfigVar vars[MAX_VARS];
int var_count = 0;

void load_config(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return;
    
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p && isspace(*p)) p++;
        if (*p == '#' || *p == '\0') continue;
        
        char *eq = strchr(p, '=');
        if (eq) {
            *eq = '\0';
            char *val = eq + 1;
            // trim key
            char *k = p + strlen(p) - 1;
            while (k > p && isspace(*k)) *k-- = '\0';
            
            // trim val
            while (*val && isspace(*val)) val++;
            char *v = val + strlen(val) - 1;
            while (v > val && isspace(*v)) *v-- = '\0';
            
            if (var_count < MAX_VARS) {
                strncpy(vars[var_count].key, p, 63);
                strncpy(vars[var_count].value, val, 63);
                var_count++;
            }
        }
    }
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <Kconfig> <.config> <autoconf.h> <config.mk>\n", argv[0]);
        return 1;
    }

    // argv[1] is Kconfig (ignored for now, assuming .config has everything we need or defaults)
    load_config(argv[2]);

    // Generate autoconf.h
    FILE *h = fopen(argv[3], "w");
    if (!h) { perror(argv[3]); return 1; }
    fprintf(h, "#ifndef AUTOCONF_H\n#define AUTOCONF_H\n\n");
    fprintf(h, "// Automatically generated file. Do not edit.\n\n");
    for (int i = 0; i < var_count; i++) {
        if (strcmp(vars[i].value, "y") == 0) {
            fprintf(h, "#define %s 1\n", vars[i].key);
        } else {
            fprintf(h, "// #undef %s\n", vars[i].key);
        }
    }
    fprintf(h, "\n#endif\n");
    fclose(h);

    // Generate config.mk
    FILE *mk = fopen(argv[4], "w");
    if (!mk) { perror(argv[4]); return 1; }
    fprintf(mk, "# Automatically generated file. Do not edit.\n\n");
    for (int i = 0; i < var_count; i++) {
        fprintf(mk, "%s := %s\n", vars[i].key, vars[i].value);
    }
    fclose(mk);

    return 0;
}
