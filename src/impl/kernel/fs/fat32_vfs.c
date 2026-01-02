#include "include/fat32_vfs.h"
#include "include/vfs.h"
#include "include/ahci.h"
#include "include/mm.h"
#include "include/stdio.h"
#include <stdint.h>
#include <stddef.h>

extern void kprintf(const char *format, uint32_t color, ...);

// String helpers
static size_t my_strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static void my_memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
}

static void my_memset(void *s, int c, size_t n) {
    uint8_t *p = s;
    while (n--) *p++ = (uint8_t)c;
}

static int my_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static int my_strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

// FAT32 helpers
static uint32_t fat32_get_fat_entry(struct fat32_fs *fs, uint32_t cluster) {
    if (!fs->fat_cache_valid) {
        kprintf("FAT32: FAT cache not loaded\n", 0xFFFF0000);
        return 0x0FFFFFFF;
    }
    
    uint32_t *fat = (uint32_t*)fs->fat_cache;
    return fat[cluster] & 0x0FFFFFFF;
}

static void fat32_set_fat_entry(struct fat32_fs *fs, uint32_t cluster, uint32_t value) {
    if (!fs->fat_cache_valid) return;
    
    uint32_t *fat = (uint32_t*)fs->fat_cache;
    fat[cluster] = (fat[cluster] & 0xF0000000) | (value & 0x0FFFFFFF);
}

static uint32_t fat32_cluster_to_sector(struct fat32_fs *fs, uint32_t cluster) {
    return fs->data_start_sector + (cluster - 2) * fs->bs.sectors_per_cluster;
}

// Find free cluster in FAT
static uint32_t fat32_alloc_cluster(struct fat32_fs *fs) {
    if (!fs->fat_cache_valid) return 0;
    
    uint32_t total_clusters = fs->bs.total_sectors_32 / fs->bs.sectors_per_cluster;
    
    for (uint32_t i = 2; i < total_clusters; i++) {
        if (fat32_get_fat_entry(fs, i) == 0) {
            // Mark as end of chain
            fat32_set_fat_entry(fs, i, 0x0FFFFFFF);
            
            // Write FAT back to disk
            uint32_t fat_sectors = fs->bs.fat_size_32;
            if (ahci_write_sectors(fs->fat_start_sector, fat_sectors, fs->fat_cache) != 0) {
                kprintf("FAT32: Failed to write FAT\n", 0xFFFF0000);
                return 0;
            }
            
            return i;
        }
    }
    
    kprintf("FAT32: No free clusters\n", 0xFFFF0000);
    return 0;
}

// Extend cluster chain
static uint32_t fat32_extend_chain(struct fat32_fs *fs, uint32_t last_cluster) {
    uint32_t new_cluster = fat32_alloc_cluster(fs);
    if (new_cluster == 0) return 0;
    
    // Link last cluster to new cluster
    fat32_set_fat_entry(fs, last_cluster, new_cluster);
    
    // Write FAT
    uint32_t fat_sectors = fs->bs.fat_size_32;
    ahci_write_sectors(fs->fat_start_sector, fat_sectors, fs->fat_cache);
    
    return new_cluster;
}

// Read cluster
static int fat32_read_cluster(struct fat32_fs *fs, uint32_t cluster, uint8_t *buffer) {
    uint32_t sector = fat32_cluster_to_sector(fs, cluster);
    return ahci_read_sectors(sector, fs->bs.sectors_per_cluster, buffer);
}

// Write cluster  
static int fat32_write_cluster(struct fat32_fs *fs, uint32_t cluster, const uint8_t *buffer) {
    uint32_t sector = fat32_cluster_to_sector(fs, cluster);
    return ahci_write_sectors(sector, fs->bs.sectors_per_cluster, buffer);
}

// Convert 8.3 filename to normal string (and lowercase it for aesthetics)
static void fat32_to_normal_name(const char *fat_name, char *out) {
    int i, j = 0;
    
    // Copy name part (trim spaces)
    for (i = 0; i < 8 && fat_name[i] != ' '; i++) {
        char c = fat_name[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        out[j++] = c;
    }
    
    // Add extension if present
    if (fat_name[8] != ' ') {
        out[j++] = '.';
        for (i = 8; i < 11 && fat_name[i] != ' '; i++) {
            char c = fat_name[i];
            if (c >= 'A' && c <= 'Z') c += 32;
            out[j++] = c;
        }
    }
    
    out[j] = '\0';
}

// Convert normal filename to 8.3
static void fat32_to_fat_name(const char *normal, char *out) {
    my_memset(out, ' ', 11);
    
    int i = 0, j = 0;
    
    // Copy name part (up to 8 chars or '.')
    while (normal[i] && normal[i] != '.' && j < 8) {
        out[j++] = (normal[i] >= 'a' && normal[i] <= 'z') ? 
                    normal[i] - 32 : normal[i]; // Uppercase
        i++;
    }
    
    // Find extension
    while (normal[i] && normal[i] != '.') i++;
    if (normal[i] == '.') {
        i++; // Skip dot
        j = 8;
        while (normal[i] && j < 11) {
            out[j++] = (normal[i] >= 'a' && normal[i] <= 'z') ?
                        normal[i] - 32 : normal[i];
            i++;
        }
    }
}

// VFS operations

static int fat32_open(struct vfs_node *node, uint32_t flags) {
    // Nothing special needed for FAT32
    return 0;
}

static void fat32_close(struct vfs_node *node) {
    // Nothing special needed
}

static int fat32_read(struct vfs_node *node, uint64_t offset, uint32_t size, uint8_t *buffer) {
    struct fat32_node_data *data = (struct fat32_node_data*)node->fs_data;
    if (!data) return -1;
    
    struct fat32_fs *fs = data->fs;
    uint32_t cluster_size = fs->bs.bytes_per_sector * fs->bs.sectors_per_cluster;
    
    // Don't read past end of file
    if (offset >= node->size) return 0;
    if (offset + size > node->size) {
        size = node->size - offset;
    }
    
    uint32_t bytes_read = 0;
    uint32_t current_cluster = data->first_cluster;
    
    // Skip to starting cluster
    uint32_t skip_clusters = offset / cluster_size;
    for (uint32_t i = 0; i < skip_clusters; i++) {
        current_cluster = fat32_get_fat_entry(fs, current_cluster);
        if (current_cluster >= 0x0FFFFFF8) return bytes_read; // EOC
    }
    
    uint32_t cluster_offset = offset % cluster_size;
    uint8_t *cluster_buf = (uint8_t*)pfa_alloc();
    
    while (size > 0 && current_cluster < 0x0FFFFFF8) {
        // Read cluster
        if (fat32_read_cluster(fs, current_cluster, cluster_buf) != 0) {
            pfa_free((uint64_t)cluster_buf);
            return bytes_read;
        }
        
        uint32_t copy_size = cluster_size - cluster_offset;
        if (copy_size > size) copy_size = size;
        
        my_memcpy(buffer + bytes_read, cluster_buf + cluster_offset, copy_size);
        bytes_read += copy_size;
        size -= copy_size;
        cluster_offset = 0;
        
        current_cluster = fat32_get_fat_entry(fs, current_cluster);
    }
    
    pfa_free((uint64_t)cluster_buf);
    return bytes_read;
}

static int fat32_write(struct vfs_node *node, uint64_t offset, uint32_t size, const uint8_t *buffer) {
    struct fat32_node_data *data = (struct fat32_node_data*)node->fs_data;
    if (!data) return -1;
    
    struct fat32_fs *fs = data->fs;
    uint32_t cluster_size = fs->bs.bytes_per_sector * fs->bs.sectors_per_cluster;
    
    uint32_t bytes_written = 0;
    uint32_t current_cluster = data->first_cluster;
    
    // If file is empty, allocate first cluster
    if (current_cluster == 0 || current_cluster >= 0x0FFFFFF8) {
        current_cluster = fat32_alloc_cluster(fs);
        if (current_cluster == 0) return -1;
        data->first_cluster = current_cluster;
    }
    
    // Skip to starting cluster
    uint32_t skip_clusters = offset / cluster_size;
    uint32_t prev_cluster = current_cluster;
    
    for (uint32_t i = 0; i < skip_clusters; i++) {
        prev_cluster = current_cluster;
        current_cluster = fat32_get_fat_entry(fs, current_cluster);
        
        // Need to extend chain?
        if (current_cluster >= 0x0FFFFFF8) {
            current_cluster = fat32_extend_chain(fs, prev_cluster);
            if (current_cluster == 0) return bytes_written;
        }
    }
    
    uint32_t cluster_offset = offset % cluster_size;
    uint8_t *cluster_buf = (uint8_t*)pfa_alloc();
    
    while (size > 0) {
        // Read existing cluster data (for partial writes)
        if (cluster_offset != 0 || size < cluster_size) {
            fat32_read_cluster(fs, current_cluster, cluster_buf);
        }
        
        uint32_t copy_size = cluster_size - cluster_offset;
        if (copy_size > size) copy_size = size;
        
        my_memcpy(cluster_buf + cluster_offset, buffer + bytes_written, copy_size);
        
        // Write cluster back
        if (fat32_write_cluster(fs, current_cluster, cluster_buf) != 0) {
            pfa_free((uint64_t)cluster_buf);
            return bytes_written;
        }
        
        bytes_written += copy_size;
        size -= copy_size;
        cluster_offset = 0;
        
        if (size > 0) {
            prev_cluster = current_cluster;
            current_cluster = fat32_get_fat_entry(fs, current_cluster);
            
            // Extend chain if needed
            if (current_cluster >= 0x0FFFFFF8) {
                current_cluster = fat32_extend_chain(fs, prev_cluster);
                if (current_cluster == 0) {
                    pfa_free((uint64_t)cluster_buf);
                    return bytes_written;
                }
            }
        }
    }
    
    pfa_free((uint64_t)cluster_buf);
    
    // Update file size if we wrote past end
    if (offset + bytes_written > node->size) {
        node->size = offset + bytes_written;
        // TODO: Update directory entry on disk
    }
    
    return bytes_written;
}

// Forward declarations
static struct vfs_node* fat32_readdir(struct vfs_node *node, uint32_t index);
static int fat32_read(struct vfs_node *node, uint64_t offset, uint32_t size, uint8_t *buffer);
static int fat32_write(struct vfs_node *node, uint64_t offset, uint32_t size, const uint8_t *buffer);
static int fat32_open(struct vfs_node *node, uint32_t flags);
static void fat32_close(struct vfs_node *node);
static int fat32_create(struct vfs_node *parent, const char *name, uint32_t flags);
static int fat32_mkdir_op(struct vfs_node *parent, const char *name);

// Helper for string copy
static char* my_strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

static char* my_strncpy(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (n > 0 && *src) {
        *d++ = *src++;
        n--;
    }
    while (n > 0) {
        *d++ = '\0';
        n--;
    }
    return dest;
}

// Helper for case-insensitive comparison
static int my_strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
        if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
        if (c1 != c2) return c1 - c2;
        s1++; s2++;
    }
    return *s1 - *s2;
}

static struct vfs_node* fat32_finddir(struct vfs_node *node, const char *name) {
    struct fat32_node_data *data = (struct fat32_node_data*)node->fs_data;
    if (!data) return NULL;
    
    struct fat32_fs *fs = data->fs;
    uint32_t cluster = data->first_cluster;
    uint32_t cluster_size = fs->bs.bytes_per_sector * fs->bs.sectors_per_cluster;
    uint32_t entries_per_cluster = cluster_size / sizeof(struct fat32_dir_entry);
    
    uint8_t *cluster_buf = (uint8_t*)pfa_alloc();
    if (!cluster_buf) return NULL;

    // Buffer for LFN
    char lfn_buf[256];
    my_memset(lfn_buf, 0, 256);
    int lfn_checksum = -1;

    while (cluster < 0x0FFFFFF8) {
        if (fat32_read_cluster(fs, cluster, cluster_buf) != 0) {
            pfa_free((uint64_t)cluster_buf);
            return NULL;
        }

        struct fat32_dir_entry *entries = (struct fat32_dir_entry*)cluster_buf;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            struct fat32_dir_entry *entry = &entries[i];

            // End of directory
            if (entry->name[0] == 0x00) {
                pfa_free((uint64_t)cluster_buf);
                return NULL;
            }

            // Skip deleted
            if (entry->name[0] == 0xE5) {
                lfn_checksum = -1;
                my_memset(lfn_buf, 0, 256);
                continue;
            }
            
            // LFN Entry
            if ((entry->attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
                struct fat32_lfn_entry *lfn = (struct fat32_lfn_entry*)entry;
                if (lfn->order & 0x40) {
                    my_memset(lfn_buf, 0, 256);
                    lfn_checksum = lfn->checksum;
                }
                
                int idx = ((lfn->order & 0x3F) - 1) * 13;
                if (idx >= 0 && idx < 242) {
                    int char_idx = 0;
                    for (int k = 0; k < 5; k++) lfn_buf[idx + char_idx++] = (lfn->name1[k] < 0x80) ? lfn->name1[k] : '?';
                    for (int k = 0; k < 6; k++) lfn_buf[idx + char_idx++] = (lfn->name2[k] < 0x80) ? lfn->name2[k] : '?';
                    for (int k = 0; k < 2; k++) lfn_buf[idx + char_idx++] = (lfn->name3[k] < 0x80) ? lfn->name3[k] : '?';
                }
                continue;
            }

            // Normal Entry
            if (entry->attr & FAT_ATTR_VOLUME_ID) {
                lfn_checksum = -1;
                continue;
            }
            
            // Match against LFN OR SFN
            uint8_t sum = 0;
            for (int k = 0; k < 11; k++) {
                sum = (((sum & 1) << 7) | ((sum & 0xFE) >> 1)) + entry->name[k];
            }
            
            int match = 0;
            if (lfn_buf[0] != 0 && sum == lfn_checksum) {
                // Match LFN
                if (my_strcasecmp(name, lfn_buf) == 0) match = 1;
            } 
            
            // Try SFN if LFN didn't match or wasn't present
            if (!match) {
                char entry_name[13];
                fat32_to_normal_name(entry->name, entry_name);
                if (my_strcasecmp(name, entry_name) == 0) match = 1;
            }
            
            if (match) {
                // Found it! Create VFS node
                struct vfs_node *child = (struct vfs_node*)pfa_alloc();
                my_memset(child, 0, sizeof(struct vfs_node));
                
                // Prefer LFN name
                if (lfn_buf[0] != 0 && sum == lfn_checksum) {
                    my_strncpy(child->name, lfn_buf, 127);
                } else {
                    fat32_to_normal_name(entry->name, child->name);
                }

                child->size = entry->file_size;
                child->flags = (entry->attr & FAT_ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
                
                child->open = fat32_open;
                child->close = fat32_close;
                child->read = fat32_read;
                child->write = fat32_write;
                child->readdir = fat32_readdir;
                child->finddir = fat32_finddir; 
                child->create = fat32_create;
                child->mkdir = fat32_mkdir_op; 
                
                struct fat32_node_data *child_data = (struct fat32_node_data*)pfa_alloc();
                child_data->first_cluster = ((uint32_t)entry->first_cluster_high << 16) | entry->first_cluster_low;
                child_data->parent_cluster = data->first_cluster;
                child_data->fs = fs;
                child->fs_data = child_data;
                
                pfa_free((uint64_t)cluster_buf);
                return child;
            }
            
            lfn_checksum = -1;
            my_memset(lfn_buf, 0, 256);
        }

        // Next cluster
        cluster = fat32_get_fat_entry(fs, cluster);
    }
    
    pfa_free((uint64_t)cluster_buf);
    return NULL;
}

static struct vfs_node* fat32_readdir(struct vfs_node *node, uint32_t index) {
    struct fat32_node_data *data = (struct fat32_node_data*)node->fs_data;
    if (!data) return NULL;
    
    struct fat32_fs *fs = data->fs;
    uint32_t cluster = data->first_cluster;
    uint32_t cluster_size = fs->bs.bytes_per_sector * fs->bs.sectors_per_cluster;
    uint32_t entries_per_cluster = cluster_size / sizeof(struct fat32_dir_entry);
    
    // Buffer for LFN
    // LFN entries come in reverse order. We can have up to 20 entries (255 chars).
    // Let's implement basic support (up to 255 chars).
    char lfn_buf[256];
    my_memset(lfn_buf, 0, 256);
    int lfn_checksum = -1;
    
    // Allocate cluster buffer
    uint8_t *cluster_buf = (uint8_t*)pfa_alloc();
    if (!cluster_buf) return NULL;
    
    // Note: This naive index skipping is problematic for LFN because LFNs take up multiple slots.
    // Ideally index should be "file index", not "directory entry index".
    // For now, we will simply scan from the beginning and count valid files.
    
    uint32_t current_file_idx = 0;
    
    while (cluster < 0x0FFFFFF8) {
        if (fat32_read_cluster(fs, cluster, cluster_buf) != 0) {
            pfa_free((uint64_t)cluster_buf);
            return NULL;
        }
        
        struct fat32_dir_entry *entries = (struct fat32_dir_entry*)cluster_buf;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            struct fat32_dir_entry *entry = &entries[i];
            
            // End of dir
            if (entry->name[0] == 0x00) {
                pfa_free((uint64_t)cluster_buf);
                return NULL;
            }
            
            // Deleted
            if (entry->name[0] == 0xE5) {
                lfn_checksum = -1; // Invalidate LFN
                my_memset(lfn_buf, 0, 256);
                continue;
            }
            
            // LFN Entry
            if ((entry->attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
                struct fat32_lfn_entry *lfn = (struct fat32_lfn_entry*)entry;
                if (lfn->order & 0x40) {
                    // Last LFN entry (start of name)
                    my_memset(lfn_buf, 0, 256);
                    lfn_checksum = lfn->checksum;
                }
                
                // Index is (order & 0x3F) - 1. Each entry has 13 chars.
                int idx = ((lfn->order & 0x3F) - 1) * 13;
                if (idx >= 0 && idx < 242) { // Bounds check
                    // Extract characters (UCS-2 to ASCII simplified)
                    int char_idx = 0;
                    for (int k = 0; k < 5; k++) lfn_buf[idx + char_idx++] = (lfn->name1[k] < 0x80) ? lfn->name1[k] : '?';
                    for (int k = 0; k < 6; k++) lfn_buf[idx + char_idx++] = (lfn->name2[k] < 0x80) ? lfn->name2[k] : '?';
                    for (int k = 0; k < 2; k++) lfn_buf[idx + char_idx++] = (lfn->name3[k] < 0x80) ? lfn->name3[k] : '?';
                }
                continue;
            }
            
            // Normal Entry (Volume ID skipped)
            if (entry->attr & FAT_ATTR_VOLUME_ID) {
                lfn_checksum = -1;
                continue;
            }
            
            // Found a valid file/dir
            if (current_file_idx == index) {
                // Return this one
                 struct vfs_node *child = (struct vfs_node*)pfa_alloc();
                 my_memset(child, 0, sizeof(struct vfs_node));
                 
                 // Use LFN if valid
                 // Checksum verification
                 uint8_t sum = 0;
                 for (int k = 0; k < 11; k++) {
                     sum = (((sum & 1) << 7) | ((sum & 0xFE) >> 1)) + entry->name[k];
                 }
                 
                 if (lfn_buf[0] != 0 && sum == lfn_checksum) {
                     my_strncpy(child->name, lfn_buf, 127); // LFN!
                 } else {
                     fat32_to_normal_name(entry->name, child->name); // SFN
                 }
                 
                 child->size = entry->file_size;
                 child->flags = (entry->attr & FAT_ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
                 
                 child->open = fat32_open;
                 child->close = fat32_close;
                 child->read = fat32_read;
                 child->write = fat32_write;
                 child->readdir = fat32_readdir;
                 child->finddir = fat32_finddir;
                 child->create = fat32_create;
                 child->mkdir = fat32_mkdir_op; 
                 
                 struct fat32_node_data *child_data = (struct fat32_node_data*)pfa_alloc();
                 child_data->first_cluster = ((uint32_t)entry->first_cluster_high << 16) | entry->first_cluster_low;
                 child_data->parent_cluster = data->first_cluster;
                 child_data->fs = fs;
                 child->fs_data = child_data;
                 
                 pfa_free((uint64_t)cluster_buf);
                 return child;
            }
            
            // Reset LFN state for next file
            lfn_checksum = -1;
            my_memset(lfn_buf, 0, 256);
            current_file_idx++;
        }
        
        // Next cluster
        cluster = fat32_get_fat_entry(fs, cluster);
    }
    
    pfa_free((uint64_t)cluster_buf);
    return NULL;
}

// Checksum for SFN
static uint8_t lfn_checksum(const unsigned char *short_name) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = (((sum & 1) << 7) | ((sum & 0xFE) >> 1)) + short_name[i];
    }
    return sum;
}

// Convert string to uppercase 8.3 name
static void make_short_name(const char *long_name, char *short_name) {
    // Very basic SFN generation: FILE~1.EXT
    // Real implementation would check for collisions, but for now we assume simple mapping
    // or just truncate if it fits. 
    // This is a simplified version.
    
    my_memset(short_name, ' ', 11);
    
    // Check if it fits directly into 8.3 (and is all valid chars)
    // For now, ALWAYS use LFN logic if length > 8 or contains dot in middle, 
    // BUT we need a base short name.
    
    // Copy up to 6 chars, uppercase
    int i = 0, j = 0;
    while (long_name[i] && long_name[i] != '.' && j < 6) {
        char c = long_name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c != ' ' && c != '.') short_name[j++] = c;
        i++;
    }
    
    // Add ~1
    short_name[6] = '~';
    short_name[7] = '1';
    
    // Find extension
    const char *ext = long_name;
    while(*ext) ext++;
    while(ext > long_name && *ext != '.') ext--;
    
    if (*ext == '.') {
        ext++;
        j = 8;
        int k = 0;
        while (ext[k] && j < 11) {
            char c = ext[k];
            if (c >= 'a' && c <= 'z') c -= 32;
            short_name[j++] = c;
            k++;
        }
    }
}

// Helper to create a directory entry (supporting LFN)
static int fat32_create_entry(struct vfs_node *parent, const char *name, uint8_t attr) {
    struct fat32_node_data *data = (struct fat32_node_data*)parent->fs_data;
    if (!data) return -1;
    struct fat32_fs *fs = data->fs;
    
    int name_len = my_strlen(name);
    int is_lfn = (name_len > 8); // Simple check, strictly 8.3 constraint is harder
    if (!is_lfn) {
        // Checking for special chars or lowercase would be better
        for(int i=0; i<name_len; i++) {
             if(name[i] == '.' && i > 8) is_lfn=1;
             if(name[i] >= 'a' && name[i] <= 'z') is_lfn=1;
        }
    }

    char short_name[11];
    if (is_lfn) {
        make_short_name(name, short_name);
    } else {
        fat32_to_fat_name(name, short_name);
    }
    
    uint8_t chksum = lfn_checksum((unsigned char*)short_name);
    
    // Calculate number of entries needed
    // 1 for SFN + (length + 12) / 13 for LFN
    int lfn_entries = 0;
    if (is_lfn) {
        lfn_entries = (name_len + 12) / 13;
    }
    int total_entries_needed = 1 + lfn_entries;
    
    // Find contiguous free entries
    uint32_t cluster = data->first_cluster;
    uint32_t cluster_size = fs->bs.bytes_per_sector * fs->bs.sectors_per_cluster;
    uint32_t entries_per_cluster = cluster_size / sizeof(struct fat32_dir_entry);
    
    uint8_t *cluster_buf = (uint8_t*)pfa_alloc();
    if (!cluster_buf) return -1;
    
    uint32_t target_cluster = 0;
    uint32_t target_start_idx = 0;
    int found_consecutive = 0;
    
    while (cluster < 0x0FFFFFF8) {
        if (fat32_read_cluster(fs, cluster, cluster_buf) != 0) {
            pfa_free((uint64_t)cluster_buf);
            return -1;
        }
        
        struct fat32_dir_entry *entries = (struct fat32_dir_entry*)cluster_buf;
        
        // Scan for N consecutive free slots
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
                // Found a free slot, check if we have enough space following it
                // Note: spanning clusters for contiguous entries is tricky. 
                // We will limit to finding contiguous slots WITHIN a cluster for simplicity safe.
                
                int free_count = 0;
                for (uint32_t j = i; j < entries_per_cluster; j++) {
                     if (entries[j].name[0] == 0x00 || entries[j].name[0] == 0xE5) {
                         free_count++;
                         if (free_count == total_entries_needed) {
                             target_cluster = cluster;
                             target_start_idx = i;
                             found_consecutive = 1;
                             break;
                         }
                     } else {
                         break; // Sequence broken
                     }
                }
                if (found_consecutive) break;
            }
        }
        
        if (found_consecutive) break;
        
        uint32_t next = fat32_get_fat_entry(fs, cluster);
        if (next >= 0x0FFFFFF8) {
             // Extend directory
             uint32_t new_cluster = fat32_extend_chain(fs, cluster);
             if (new_cluster == 0) {
                 pfa_free((uint64_t)cluster_buf);
                 return -1;
             }
             my_memset(cluster_buf, 0, 4096);
             fat32_write_cluster(fs, new_cluster, cluster_buf);
             
             // The new cluster is definitely empty enough
             target_cluster = new_cluster;
             target_start_idx = 0;
             found_consecutive = 1;
             break;
        }
        cluster = next;
    }
    
    if (!found_consecutive) {
        pfa_free((uint64_t)cluster_buf);
        return -1;
    }
    
    // Prepare allocation for the file content (if needed)
    uint32_t first_cluster = 0;
    if (attr & FAT_ATTR_DIRECTORY) {
        first_cluster = fat32_alloc_cluster(fs);
        if (first_cluster == 0) {
            pfa_free((uint64_t)cluster_buf);
            return -1;
        }
        // Initialize new directory with . and ..
        uint8_t *new_dir_buf = (uint8_t*)pfa_alloc();
        my_memset(new_dir_buf, 0, 4096);
        struct fat32_dir_entry *dot = (struct fat32_dir_entry*)new_dir_buf;
        
        my_memset(dot[0].name, ' ', 11); dot[0].name[0] = '.'; dot[0].attr = FAT_ATTR_DIRECTORY;
        dot[0].first_cluster_high = (first_cluster >> 16) & 0xFFFF;
        dot[0].first_cluster_low = first_cluster & 0xFFFF;
        
        my_memset(dot[1].name, ' ', 11); dot[1].name[0] = '.'; dot[1].name[1] = '.'; dot[1].attr = FAT_ATTR_DIRECTORY;
        uint32_t p = data->first_cluster;
        if (p == fs->root_dir_cluster) p = 0;
        dot[1].first_cluster_high = (p >> 16) & 0xFFFF;
        dot[1].first_cluster_low = p & 0xFFFF;
        
        fat32_write_cluster(fs, first_cluster, new_dir_buf);
        pfa_free((uint64_t)new_dir_buf);
    }
    
    // Re-read cluster to be safe
    fat32_read_cluster(fs, target_cluster, cluster_buf);
    struct fat32_dir_entry *entries = (struct fat32_dir_entry*)cluster_buf;
    
    // Write LFN entries (reverse order)
    if (is_lfn) {
        int current_entry = target_start_idx;
        for (int i = lfn_entries; i > 0; i--) {
            struct fat32_lfn_entry *lfn = (struct fat32_lfn_entry*)&entries[current_entry++];
            my_memset(lfn, 0, sizeof(struct fat32_lfn_entry));
            
            lfn->order = i;
            if (i == lfn_entries) lfn->order |= 0x40; // Last entry marker
            lfn->attr = FAT_ATTR_LFN;
            lfn->type = 0;
            lfn->checksum = chksum;
            
            // Fill name chars (13 chars per entry)
            int start_char = (i - 1) * 13;
            int len = name_len - start_char;
            if (len > 13) len = 13;
            
            const char *part = name + start_char;
            
            // name1 (5 chars)
            for (int k=0; k<5; k++) lfn->name1[k] = (k < len) ? (uint16_t)part[k] : ((k == len) ? 0 : 0xFFFF);
            // name2 (6 chars)
            for (int k=0; k<6; k++) lfn->name2[k] = (k+5 < len) ? (uint16_t)part[k+5] : ((k+5 == len) ? 0 : 0xFFFF);
            // name3 (2 chars)
            for (int k=0; k<2; k++) lfn->name3[k] = (k+11 < len) ? (uint16_t)part[k+11] : ((k+11 == len) ? 0 : 0xFFFF);
        }
        
        // Write SFN entry at the end position
        struct fat32_dir_entry *sfn = &entries[target_start_idx + lfn_entries];
        my_memset(sfn, 0, sizeof(struct fat32_dir_entry));
        my_memcpy(sfn->name, short_name, 11);
        sfn->attr = attr;
        sfn->first_cluster_high = (first_cluster >> 16) & 0xFFFF;
        sfn->first_cluster_low = first_cluster & 0xFFFF;
        sfn->file_size = 0;
        
    } else {
        // Just SFN
        struct fat32_dir_entry *sfn = &entries[target_start_idx];
        my_memset(sfn, 0, sizeof(struct fat32_dir_entry));
        my_memcpy(sfn->name, short_name, 11);
        sfn->attr = attr;
        sfn->first_cluster_high = (first_cluster >> 16) & 0xFFFF;
        sfn->first_cluster_low = first_cluster & 0xFFFF;
        sfn->file_size = 0;
    }

    // Write back
    fat32_write_cluster(fs, target_cluster, cluster_buf);
    pfa_free((uint64_t)cluster_buf);

    return 0;
}

static int fat32_create(struct vfs_node *parent, const char *name, uint32_t flags) {
    if (!parent || !(parent->flags & VFS_DIRECTORY)) return -1;
    
    // Check if it already exists
    struct vfs_node *existing = fat32_finddir(parent, name);
    if (existing) {
        // It exists
        // If we wanted to be robust, we'd check if it's a dir vs file match etc.
        // For now, fail if anything exists with that name.
        // We must free the node returned by finddir since it was allocated!
        if (existing->fs_data) pfa_free((uint64_t)existing->fs_data);
        pfa_free((uint64_t)existing);
        return -1; // EEXIST
    }
    
    uint8_t attr = (flags & VFS_DIRECTORY) ? FAT_ATTR_DIRECTORY : FAT_ATTR_ARCHIVE;
    return fat32_create_entry(parent, name, attr);
}

// forward decl for wrapper
static int fat32_mkdir_op(struct vfs_node *parent, const char *name);

static int fat32_mkdir_op(struct vfs_node *parent, const char *name) {
    return fat32_create(parent, name, VFS_DIRECTORY);
}

// Helper to parse hex digit
static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

// Parse "XXXX-XXXX" into uint32_t
// Note: blkid prints it as: high-16 bits (dash) low-16 bits ?
// Or simply as bytes? 
// The volume ID in FAT32 is just a 32-bit number.
// Usually blkid displays it as two groups of 4 hex digits.
// Example: F6DD-210C -> 0xF6DD210C or 0x210CF6DD ?
// Testing shows it's often effectively the big-endian representation of the 32-bit int if viewed as string, or just the bytes reversed.
// Let's assume input string "ABCD-EF01" corresponds to byte sequence [01, EF, CD, AB] (Little Endian on disk)
// If fs->bs.volume_id is 0xEF01ABCD.
// Let's try to match the hex string directly to the number.
static uint32_t parse_volume_id(const char *s) {
    uint32_t val = 0;
    while (*s) {
        if (*s == '-') { s++; continue; }
        val = (val << 4) | hex_val(*s);
        s++;
    }
    return val;
}

// Mount function
static int fat32_mount(const char *device, struct mount_point *mp) {
    kprintf("FAT32: Mounting device...\n", 0x00FF0000);
    
    kprintf("FAT32: Step 1 - Allocating FS structure\n", 0x0000FFFF);
    // Allocate filesystem private data
    struct fat32_fs *fs = (struct fat32_fs*)pfa_alloc();
    if (!fs) {
        kprintf("FAT32: Failed to allocate FS structure\n", 0xFFFF0000);
        return -1;
    }
    my_memset(fs, 0, sizeof(struct fat32_fs));
    kprintf("FAT32: FS structure allocated at 0x%lx\n", 0x0000FFFF, (uint64_t)fs);
    
    kprintf("FAT32: Step 2 - Reading boot sector\n", 0x0000FFFF);
    // Read boot sector
    uint8_t *boot_sector = (uint8_t*)pfa_alloc();
    if (!boot_sector) {
        kprintf("FAT32: Failed to allocate boot sector buffer\n", 0xFFFF0000);
        pfa_free((uint64_t)fs);
        return -1;
    }
    
    // Read from AHCI port (implicitly port 0 via wrapper)
    // TODO: In the future, 'device' argument should dictate WHICH port.
    // For now, if device is "/dev/sdb", we can't easily switch ports yet as ahci.c exposes a global read.
    // If device is "UUID=...", we read the active port and verify check below.
    kprintf("FAT32: Step 3 - AHCI read sector 0\n", 0x0000FFFF);
    if (ahci_read_sectors(0, 1, boot_sector) != 0) {
        kprintf("FAT32: Failed to read boot sector\n", 0xFFFF0000);
        pfa_free((uint64_t)boot_sector);
        pfa_free((uint64_t)fs);
        return -1;
    }
    kprintf("FAT32: Boot sector read successfully\n", 0x0000FFFF);
    
    kprintf("FAT32: Step 4 - Copying boot sector\n", 0x0000FFFF);
    my_memcpy(&fs->bs, boot_sector, sizeof(struct fat32_boot_sector));
    pfa_free((uint64_t)boot_sector);
    
    // Check UUID if requested
    if (device && my_strncmp(device, "UUID=", 5) == 0) {
        const char *uuid_str = device + 5;
        uint32_t expected_id = parse_volume_id(uuid_str);
        
        // Volume ID on disk is little-endian 32-bit integer.
        // blkid "F6DD-210C" usually means 0xF6DD210C in big-endian reading order,
        // which matches the uint32_t value if we construct it from the hex string.
        
        // HOWEVER: Linux blkid treats the volume ID field as follows:
        // offset 67 (0x43): 4 bytes.
        // If bytes are [0x0C, 0x21, 0xDD, 0xF6], little endian value is 0xF6DD210C.
        // And blkid prints "F6DD-210C".
        // So fs->bs.volume_id (read as u32) should match our parsed value.
        
        kprintf("FAT32: Checking Volume ID. Expected: %x, Found: %x\n", 0x00FFFF00, expected_id, fs->bs.volume_id);
        
        // Note: fs->bs.volume_id is already read into machine endianness (since we did memcpy from disk, and x86 is little endian, same as FAT).
        // So `uint32_t volume_id` holds the correct integer value.
        // But let's verify if byte swapping is needed for the string comparison.
        // "F6DD-210C" parsed as hex -> 0xF6DD210C.
        // Disk (LE): 0C 21 DD F6.
        // Memory (u32 on x86): 0xF6DD210C.
        // So they should match directly.
        
        if (fs->bs.volume_id != expected_id) {
            kprintf("FAT32: Volume ID mismatch!\n", 0xFFFF0000);
            pfa_free((uint64_t)fs);
            return -1;
        }
    }
    
    kprintf("FAT32: Step 5 - Validating boot sector\n", 0x0000FFFF);
    kprintf("FAT32:   Bytes per sector: %d\n", 0x00FFFF00, fs->bs.bytes_per_sector);
    kprintf("FAT32:   Sectors per cluster: %d\n", 0x00FFFF00, fs->bs.sectors_per_cluster);
    kprintf("FAT32:   Reserved sectors: %d\n", 0x00FFFF00, fs->bs.reserved_sectors);
    kprintf("FAT32:   Number of FATs: %d\n", 0x00FFFF00, fs->bs.num_fats);
    kprintf("FAT32:   FAT size (sectors): %d\n", 0x00FFFF00, fs->bs.fat_size_32);
    kprintf("FAT32:   Root cluster: %d\n", 0x00FFFF00, fs->bs.root_cluster);
    
    // Validate
    if (fs->bs.bytes_per_sector != 512) {
        kprintf("FAT32: Unsupported sector size: %d\n", 0xFFFF0000, fs->bs.bytes_per_sector);
        pfa_free((uint64_t)fs);
        return -1;
    }
    
    if (fs->bs.sectors_per_cluster == 0 || fs->bs.sectors_per_cluster > 128) {
        kprintf("FAT32: Invalid sectors per cluster: %d\n", 0xFFFF0000, fs->bs.sectors_per_cluster);
        pfa_free((uint64_t)fs);
        return -1;
    }
    
    kprintf("FAT32: Step 6 - Calculating offsets\n", 0x0000FFFF);
    // Calculate important offsets
    fs->fat_start_sector = fs->bs.reserved_sectors;
    fs->data_start_sector = fs->bs.reserved_sectors + 
                            (fs->bs.num_fats * fs->bs.fat_size_32);
    fs->root_dir_cluster = fs->bs.root_cluster;
    
    kprintf("FAT32:   FAT starts at sector: %d\n", 0x00FFFF00, fs->fat_start_sector);
    kprintf("FAT32:   Data starts at sector: %d\n", 0x00FFFF00, fs->data_start_sector);
    
    kprintf("FAT32: Step 7 - Allocating FAT cache\n", 0x0000FFFF);
    // Read FAT into cache - CRITICAL FIX: allocate enough pages!
    uint32_t fat_size_bytes = fs->bs.fat_size_32 * 512;
    uint32_t fat_pages = (fat_size_bytes + 4095) / 4096;
    
    kprintf("FAT32:   FAT size: %d bytes (%d sectors, %d pages)\n", 0x00FFFF00, 
            fat_size_bytes, fs->bs.fat_size_32, fat_pages);
    
    // For now, only cache first page of FAT (4KB = 8 sectors)
    // This limits us but prevents memory issues
    uint32_t sectors_to_read = (fat_size_bytes > 4096) ? 8 : fs->bs.fat_size_32;
    
    kprintf("FAT32:   Allocating 1 page, reading %d sectors\n", 0x00FFFF00, sectors_to_read);
    fs->fat_cache = (uint8_t*)pfa_alloc();
    if(!fs->fat_cache) {
        kprintf("FAT32: Failed to allocate FAT cache\n", 0xFFFF0000);
        pfa_free((uint64_t)fs);
        return -1;
    }
    
    kprintf("FAT32: Step 8 - Reading FAT from disk\n", 0x0000FFFF);
    if (ahci_read_sectors(fs->fat_start_sector, sectors_to_read, fs->fat_cache) != 0) {
        kprintf("FAT32: Failed to read FAT\n", 0xFFFF0000);
        pfa_free((uint64_t)fs->fat_cache);
        pfa_free((uint64_t)fs);
        return -1;
    }
    fs->fat_cache_valid = 1;
    
    kprintf("FAT32: FAT loaded into cache\n", 0x00FF0000);
    
    kprintf("FAT32: Step 9 - Creating root VFS node\n", 0x0000FFFF);
    // Create root VFS node
    struct vfs_node *root = (struct vfs_node*)pfa_alloc();
    if (!root) {
        kprintf("FAT32: Failed to allocate root node\n", 0xFFFF0000);
        pfa_free((uint64_t)fs->fat_cache);
        pfa_free((uint64_t)fs);
        return -1;
    }
    my_memset(root, 0, sizeof(struct vfs_node));
    
    my_memcpy(root->name, "/", 2);
    root->flags = VFS_DIRECTORY;
    root->size = 0;
    
    root->open = fat32_open;
    root->close = fat32_close;
    root->read = fat32_read;
    root->write = fat32_write;
    root->readdir = fat32_readdir;
    root->finddir = fat32_finddir; 
    root->create = fat32_create;
    root->mkdir = fat32_mkdir_op; 
    
    kprintf("FAT32: Step 10 - Creating root node data\n", 0x0000FFFF);
    struct fat32_node_data *root_data = (struct fat32_node_data*)pfa_alloc();
    if (!root_data) {
        kprintf("FAT32: Failed to allocate root node data\n", 0xFFFF0000);
        pfa_free((uint64_t)root);
        pfa_free((uint64_t)fs->fat_cache);
        pfa_free((uint64_t)fs);
        return -1;
    }
    root_data->first_cluster = fs->root_dir_cluster;
    root_data->parent_cluster = 0;
    root_data->fs = fs;
    root->fs_data = root_data;
    
    kprintf("FAT32: Step 11 - Setting mount point\n", 0x0000FFFF);
    mp->root = root;
    mp->fs_private = fs;
    
    kprintf("FAT32: Mount successful!\n", 0x00FF0000);
    return 0;
}

static int fat32_unmount(struct mount_point *mp) {
    // TODO: Flush caches, free memory
    return 0;
}

void fat32_register(void) {
    vfs_register_filesystem("fat32", fat32_mount, fat32_unmount);
}
