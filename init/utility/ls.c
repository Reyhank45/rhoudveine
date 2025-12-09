#include <stdint.h>
#include "util.h"

/* Declare the output helpers provided by `init/init.c` as externs. */
extern void out_puts(const char *s);
extern void out_putchar(char c);

#include "../../src/impl/kernel/fat32.h"
#include "../../src/impl/kernel/init_fs.h"

// List directory entries in the given path. If no embedded FAT32 image is
// available, prints a message.
void util_ls(const char *path) {
    (void)path;
    if (!embedded_fat32_image || embedded_fat32_size == 0) {
        out_puts("ls: no filesystem available (embedded FAT32 not found)\n");
        return;
    }

    struct fat32_fs fs;
    if (fat32_init_from_memory(&fs, embedded_fat32_image, embedded_fat32_size) != 0) {
        out_puts("ls: failed to init FAT32 from embedded image\n");
        return;
    }

    // Only support listing root directory for now (path == "/" or NULL)
    uint32_t cluster = fs.root_cluster;
    uint32_t offset = (cluster >= 2) ? (cluster - 2) * fs.sectors_per_cluster * fs.bytes_per_sector : 0;
    uint8_t *dir = fs.data + offset;
    uint32_t dir_bytes = fs.bytes_per_sector * fs.sectors_per_cluster;

    for (uint32_t i = 0; i < dir_bytes; i += 32) {
        uint8_t first = dir[i];
        if (first == 0x00) break;
        if (first == 0xE5) continue;
        uint8_t attr = dir[i+11];
        if (attr == 0x0F) continue; // long name
        char name[13];
        int ni = 0;
        for (int k = 0; k < 11; k++) {
            char c = dir[i+k];
            if (c == ' ') continue;
            if (k == 8) name[ni++] = '.';
            name[ni++] = c;
        }
        name[ni] = '\0';
        out_puts(name);
        out_puts("\n");
    }
}
