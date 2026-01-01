#include "include/fat32_vfs.h"
#include "include/vfs.h"
#include "include/ahci.h"
#include "include/mm.h"
#include "stdio.h"
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

// Convert 8.3 filename to normal string
static void fat32_to_normal_name(const char *fat_name, char *out) {
    int i, j = 0;
    
    // Copy name part (trim spaces)
    for (i = 0; i < 8 && fat_name[i] != ' '; i++) {
        out[j++] = fat_name[i];
    }
    
    // Add extension if present
    if (fat_name[8] != ' ') {
        out[j++] = '.';
        for (i = 8; i < 11 && fat_name[i] != ' '; i++) {
            out[j++] = fat_name[i];
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

// Helper for string copy
static char* my_strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
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

    while (cluster < 0x0FFFFFF8) {
        if (fat32_read_cluster(fs, cluster, cluster_buf) != 0) {
            pfa_free((uint64_t)cluster_buf);
            return NULL;
        }

        struct fat32_dir_entry *entries = (struct fat32_dir_entry*)cluster_buf;
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            struct fat32_dir_entry *entry = &entries[i];

            // Check for end of directory
            if (entry->name[0] == 0x00) {
                pfa_free((uint64_t)cluster_buf);
                return NULL;
            }

            // Skip deleted or LFN entries
            if (entry->name[0] == 0xE5 || (entry->attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
                continue;
            }

            // Check name match
            char entry_name[13];
            fat32_to_normal_name(entry->name, entry_name);
            
            if (my_strcasecmp(name, entry_name) == 0) {
                // Found it! Create VFS node
                struct vfs_node *child = (struct vfs_node*)pfa_alloc();
                my_memset(child, 0, sizeof(struct vfs_node));
                
                my_strcpy(child->name, entry_name);
                child->size = entry->file_size;
                child->flags = (entry->attr & FAT_ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
                
                child->open = fat32_open;
                child->close = fat32_close;
                child->read = fat32_read;
                child->write = fat32_write;
                child->readdir = fat32_readdir;
                child->finddir = fat32_finddir; // Recursively support finddir
                
                struct fat32_node_data *child_data = (struct fat32_node_data*)pfa_alloc();
                child_data->first_cluster = ((uint32_t)entry->first_cluster_high << 16) | entry->first_cluster_low;
                child_data->parent_cluster = data->first_cluster;
                child_data->fs = fs;
                child->fs_data = child_data;
                
                pfa_free((uint64_t)cluster_buf);
                return child;
            }
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
    
    // Skip to the cluster containing this index
    uint32_t target_cluster_idx = index / entries_per_cluster;
    for (uint32_t i = 0; i < target_cluster_idx && cluster < 0x0FFFFFF8; i++) {
        cluster = fat32_get_fat_entry(fs, cluster);
    }
    
    if (cluster >= 0x0FFFFFF8) return NULL;
    
    uint8_t *cluster_buf = (uint8_t*)pfa_alloc();
    if (fat32_read_cluster(fs, cluster, cluster_buf) != 0) {
        pfa_free((uint64_t)cluster_buf);
        return NULL;
    }
    
    uint32_t entry_in_cluster = index % entries_per_cluster;
    struct fat32_dir_entry *entries = (struct fat32_dir_entry*)cluster_buf;
    struct fat32_dir_entry *entry = &entries[entry_in_cluster];
    
    // Skip deleted/empty entries
    while (entry->name[0] == 0xE5 || entry->name[0] == 0x00 || 
           (entry->attr & FAT_ATTR_LFN) == FAT_ATTR_LFN) {
        if (entry->name[0] == 0x00) {
            pfa_free((uint64_t)cluster_buf);
            return NULL; // End of directory
        }
        index++;
        entry_in_cluster = index % entries_per_cluster;
        if (entry_in_cluster == 0) {
            cluster = fat32_get_fat_entry(fs, cluster);
            if (cluster >= 0x0FFFFFF8) {
                pfa_free((uint64_t)cluster_buf);
                return NULL;
            }
            fat32_read_cluster(fs, cluster, cluster_buf);
            entries = (struct fat32_dir_entry*)cluster_buf;
        }
        entry = &entries[entry_in_cluster];
    }
    
    if (entry->name[0] == 0x00) {
        pfa_free((uint64_t)cluster_buf);
        return NULL;
    }
    
    // Create VFS node
    struct vfs_node *child = (struct vfs_node*)pfa_alloc();
    my_memset(child, 0, sizeof(struct vfs_node));
    
    fat32_to_normal_name(entry->name, child->name);
    child->size = entry->file_size;
    child->flags = (entry->attr & FAT_ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
    
    child->open = fat32_open;
    child->close = fat32_close;
    child->read = fat32_read;
    child->write = fat32_write;
    child->readdir = fat32_readdir;
    child->finddir = fat32_finddir; 
    
    struct fat32_node_data *child_data = (struct fat32_node_data*)pfa_alloc();
    child_data->first_cluster = ((uint32_t)entry->first_cluster_high << 16) | entry->first_cluster_low;
    child_data->parent_cluster = data->first_cluster;
    child_data->fs = fs;
    child->fs_data = child_data;
    
    pfa_free((uint64_t)cluster_buf);
    return child;
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
