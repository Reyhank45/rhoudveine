#pragma once

#include <stdint.h>

// Pointer/size of a discovered FAT32 module image (set by kernel at boot)
extern uint8_t *embedded_fat32_image;
extern uint32_t embedded_fat32_size;
