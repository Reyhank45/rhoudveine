#include <stdint.h>
#include <stddef.h>
#include "fat32.h"
#include "include/console.h"

// Minimal FAT32 reader: supports short 8.3 names, traverses directories,
// reads cluster chains via FAT. Assumes image is a contiguous memory buffer.

static uint32_t read_le32(const void *p) {
    const uint8_t *b = (const uint8_t*)p;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static uint16_t read_le16(const void *p) {
    const uint8_t *b = (const uint8_t*)p;
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

int fat32_init_from_memory(struct fat32_mem_fs *fs, void *image, uint32_t size) {
    if (!fs || !image || size < 512) return -1;
    fs->data = (uint8_t*)image;
    fs->total_size = size;
    uint8_t *bpb = fs->data;
    fs->bytes_per_sector = read_le16(bpb + 11);
    fs->sectors_per_cluster = bpb[13];
    fs->reserved_sectors = read_le16(bpb + 14);
    fs->num_fats = bpb[16];
    fs->fat_size_sectors = read_le32(bpb + 36);
    fs->root_cluster = read_le32(bpb + 44);

    if (fs->bytes_per_sector == 0 || fs->sectors_per_cluster == 0) return -1;

    uint32_t first_fat_sector = fs->reserved_sectors;
    uint32_t first_data_sector = fs->reserved_sectors + (fs->num_fats * fs->fat_size_sectors);
    fs->first_data_sector = first_data_sector;
    // basic sanity: check computed size
    uint64_t total_bytes = (uint64_t)fs->bytes_per_sector * fs->sectors_per_cluster * fs->fat_size_sectors;
    // don't be too strict
    return 0;
}

static uint32_t cluster_to_offset(struct fat32_mem_fs *fs, uint32_t cluster) {
    // cluster numbering starts at 2
    uint32_t first_data_sector = fs->first_data_sector;
    uint32_t sector = first_data_sector + (cluster - 2) * fs->sectors_per_cluster;
    return sector * fs->bytes_per_sector;
}

static uint32_t fat_entry(struct fat32_mem_fs *fs, uint32_t cluster) {
    // FAT starts at reserved sectors
    uint32_t fat_offset = fs->reserved_sectors * fs->bytes_per_sector;
    uint32_t entry_offset = fat_offset + (cluster * 4);
    if (entry_offset + 4 > fs->total_size) return 0x0FFFFFFF;
    return read_le32(fs->data + entry_offset) & 0x0FFFFFFF;
}

// Compare 8.3 name (filename in dir entry) with component (case-insensitive)
static int match_short_name(const uint8_t *entry_name, const char *component) {
    // build 11-char name from component
    char name[12];
    for (int i = 0; i < 11; i++) name[i] = ' ';
    name[11] = '\0';

    const char *p = component;
    int i = 0;
    while (*p && *p != '/' && i < 8) {
        char c = *p++;
        if (c >= 'a' && c <= 'z') c -= 32;
        name[i++] = c;
    }
    if (*p == '.') {
        p++;
        int j = 8;
        while (*p && *p != '/' && j < 11) {
            char c = *p++;
            if (c >= 'a' && c <= 'z') c -= 32;
            name[j++] = c;
        }
    }

    for (int k = 0; k < 11; k++) {
        if ((char)entry_name[k] != name[k]) return 0;
    }
    return 1;
}

int fat32_open_file(struct fat32_mem_fs *fs, const char *path, uint8_t **out_ptr, uint32_t *out_size) {
    if (!fs || !path || path[0] != '/') return -1;
    // start from root cluster
    uint32_t current_cluster = fs->root_cluster;
    // tokenize path
    const char *p = path + 1; // skip '/'
    char component[256];
    while (1) {
        // extract next component
        int idx = 0;
        while (*p && *p != '/' && idx < (int)sizeof(component)-1) component[idx++] = *p++;
        component[idx] = '\0';

        // search directory entries in current_cluster
        uint32_t cluster = current_cluster;
        int found = 0;
        while (cluster < 0x0FFFFFF8) {
            uint32_t offset = cluster_to_offset(fs, cluster);
            if (offset + fs->bytes_per_sector * fs->sectors_per_cluster > fs->total_size) return -1;
            uint8_t *dir = fs->data + offset;
            uint32_t dir_bytes = fs->bytes_per_sector * fs->sectors_per_cluster;
            for (uint32_t i = 0; i < dir_bytes; i += 32) {
                uint8_t first = dir[i];
                if (first == 0x00) return -1; // no more entries
                if (first == 0xE5) continue;
                uint8_t attr = dir[i+11];
                if (attr == 0x0F) continue; // long name entry
                if (match_short_name(&dir[i], component)) {
                    // found
                    uint32_t high = read_le16(&dir[i+20]);
                    uint32_t low = read_le16(&dir[i+26]);
                    uint32_t start_cluster = (high << 16) | low;
                    uint32_t file_size = read_le32(&dir[i+28]);
                    if (*p == '\0') {
                        // last component — return pointer (if file)
                        *out_ptr = fs->data + cluster_to_offset(fs, start_cluster);
                        *out_size = file_size;
                        return 0;
                    } else {
                        // descend into directory
                        current_cluster = start_cluster;
                        found = 1;
                        break;
                    }
                }
            }
            if (found) break;
            cluster = fat_entry(fs, cluster);
        }
        if (!found) return -1;
        if (*p == '/') p++; // skip '/'
        if (*p == '\0') break;
    }
    return -1;
}
