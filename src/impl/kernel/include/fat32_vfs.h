#ifndef KERNEL_FAT32_VFS_H
#define KERNEL_FAT32_VFS_H

#include <stdint.h>
#include "vfs.h"

// FAT32 Boot Sector structure
struct fat32_boot_sector {
    uint8_t jmp[3];
    char oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    
    // FAT32-specific
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    char volume_label[11];
    char fs_type[8];
} __attribute__((packed));

// FAT32 Directory Entry
struct fat32_dir_entry {
    char name[11];
    uint8_t attr;
    uint8_t nt_reserved;
    uint8_t creation_time_tenth;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed));

// File attributes
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F

// FAT32 filesystem private data
struct fat32_fs {
    struct fat32_boot_sector bs;
    uint32_t fat_start_sector;
    uint32_t data_start_sector;
    uint32_t root_dir_cluster;
    uint8_t *fat_cache;  // Cached FAT table
    int fat_cache_valid;
};

// FAT32 node private data
struct fat32_node_data {
    uint32_t first_cluster;
    uint32_t parent_cluster;
    struct fat32_fs *fs;
};

// Register FAT32 filesystem
void fat32_register(void);

#endif // KERNEL_FAT32_VFS_H
