#pragma once

#include <stdint.h>

struct fat32_fs {
    uint8_t *data;
    uint32_t total_size;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint32_t fat_size_sectors;
    uint32_t root_cluster;
    uint32_t first_data_sector;
};

// Initialize FAT32 structure from an in-memory image
int fat32_init_from_memory(struct fat32_fs *fs, void *image, uint32_t size);

// Open a file by absolute path (e.g. "/path/to/file") — only short 8.3 names
// On success returns 0 and fills out buffer pointer (allocated inside caller's memory space as pointer into image) and size
int fat32_open_file(struct fat32_fs *fs, const char *path, uint8_t **out_ptr, uint32_t *out_size);
