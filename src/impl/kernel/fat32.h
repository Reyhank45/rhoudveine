#pragma once

#include <stdint.h>

struct fat32_mem_fs {
    void *image;
    uint32_t image_size;
    uint32_t root_dir_offset;
    uint32_t data_offset;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t fat_offset;
    uint32_t fat_size;
    uint32_t root_cluster;
};

// Initialize FAT32 structure from an in-memory image
int fat32_init_from_memory(struct fat32_mem_fs *fs, void *image, uint32_t size);
void fat32_list_directory(struct fat32_mem_fs *fs, const char *path);

// Open a file by absolute path (e.g. "/path/to/file") — only short 8.3 names
// On success returns 0 and fills out buffer pointer (allocated inside caller's memory space as pointer into image) and size
int fat32_open_file(struct fat32_mem_fs *fs, const char *path, uint8_t **out_ptr, uint32_t *out_size);
