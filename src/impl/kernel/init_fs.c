#include <stdint.h>
#include "init_fs.h"

// Initialized to NULL/0 and set by `kernel_main` when a FAT32 module is
// discovered. These symbols are referenced by `init` utilities when the
// init code is linked into the kernel.
uint8_t *embedded_fat32_image = 0;
uint32_t embedded_fat32_size = 0;
