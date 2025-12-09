#include <stdint.h>
#include "util.h"

extern void out_puts(const char *s);
extern void out_putchar(char c);

#include <stddef.h>
#include "../../src/impl/kernel/fat32.h"
#include "../../src/impl/kernel/init_fs.h"

void util_cat(const char *path) {
    if (!embedded_fat32_image || embedded_fat32_size == 0) {
        out_puts("cat: no filesystem available (embedded FAT32 not found)\n");
        return;
    }

    struct fat32_fs fs;
    if (fat32_init_from_memory(&fs, embedded_fat32_image, embedded_fat32_size) != 0) {
        out_puts("cat: failed to init FAT32 from embedded image\n");
        return;
    }

    uint8_t *fileptr = NULL;
    uint32_t filesize = 0;
    if (fat32_open_file(&fs, path, &fileptr, &filesize) != 0) {
        out_puts("cat: file not found\n");
        return;
    }

    // Print file contents (up to filesize)
    for (uint32_t i = 0; i < filesize; i++) {
        out_putchar((char)fileptr[i]);
    }
}
