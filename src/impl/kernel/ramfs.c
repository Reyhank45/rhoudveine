#include "include/vfs.h"
#include "include/vfs.h"
#include "include/mm.h"
#include <stdint.h>
#include <stddef.h>

extern void kprintf(const char *format, uint32_t color, ...);

// Simple RAMFS implementation using 4KB pages from PFA
// Limitations: 
// - Fixed number of nodes per page? No, we'll just alloc pages as needed.
// - Inefficient, uses full 4KB for even small data if we don't block.
// - For now: 1 file = 1 metadata page + N data pages.

typedef struct ramfs_node_data {
    // We can store children pointers here for directories
    // Simplicity: fixed array of children
    #define RAMFS_MAX_CHILDREN 64
    char child_names[RAMFS_MAX_CHILDREN][32];
    struct vfs_node *children[RAMFS_MAX_CHILDREN];
} ramfs_node_data_t;

// Static memory pool for ramfs (no dynamic allocation)
#define MAX_RAMFS_NODES 64
static struct vfs_node ramfs_node_pool[MAX_RAMFS_NODES];
static ramfs_node_data_t ramfs_data_pool[MAX_RAMFS_NODES]; // One data block per node (wasteful but safe)
static int ramfs_node_count = 0;

// Forward declaration
int ramfs_mkdir_op(struct vfs_node *parent, const char *name);

// Helper to create a new vfs_node from static pool
static struct vfs_node* ramfs_alloc_node(const char *name, int flags) {
    if (ramfs_node_count >= MAX_RAMFS_NODES) {
        return NULL;  // Pool exhausted
    }
    
    int index = ramfs_node_count++;
    struct vfs_node *node = &ramfs_node_pool[index];
    ramfs_node_data_t *data = &ramfs_data_pool[index];
    
    // Clear node
    for (int i = 0; i < (int)sizeof(struct vfs_node); i++) ((char*)node)[i] = 0;
    
    // Clear data
    for (int i = 0; i < RAMFS_MAX_CHILDREN; i++) {
        data->children[i] = NULL;
        data->child_names[i][0] = '\0';
    }
    
    // Copy name
    int i = 0;
    while(name[i] && i < 255) {
        node->name[i] = name[i];
        i++;
    }
    node->name[i] = '\0';
    
    node->flags = flags;
    node->size = 0;
    
    // Assign data block if directory
    if (flags & VFS_DIRECTORY) {
        node->fs_data = (void*)data;
    } else {
        node->fs_data = NULL; // Files don't have data in this simplified version yet
    }
    
    return node;
}

static struct vfs_node* ramfs_finddir(struct vfs_node *node, const char *name) {
    if (!node || !(node->flags & VFS_DIRECTORY) || !node->fs_data) return NULL;
    
    ramfs_node_data_t *data = (ramfs_node_data_t*)node->fs_data;
    
    for (int i = 0; i < RAMFS_MAX_CHILDREN; i++) {
        if (data->children[i]) {
            // Manual string compare since we don't have strcmp included here maybe?
            // Actually main.c uses my_strcmp, but we are in ramfs.c
            const char *n = data->child_names[i];
            const char *t = name;
            int match = 1;
            while(*n && *t) {
                if (*n != *t) { match = 0; break; }
                n++; t++;
            }
            if (match && *n == *t) {
                return data->children[i];
            }
        }
    }
    return NULL;
}

static struct vfs_node* ramfs_readdir(struct vfs_node *node, uint32_t index) {
    if (!node || !(node->flags & VFS_DIRECTORY) || !node->fs_data) return NULL;
    
    ramfs_node_data_t *data = (ramfs_node_data_t*)node->fs_data;
    
    int count = 0;
    for (int i = 0; i < RAMFS_MAX_CHILDREN; i++) {
        if (data->children[i]) {
            if (count == (int)index) {
                return data->children[i];
            }
            count++;
        }
    }
    return NULL;
}

static int ramfs_create(struct vfs_node *parent, const char *name, uint32_t flags) {
    if (!parent || !(parent->flags & VFS_DIRECTORY) || !parent->fs_data) return -1;
    
    ramfs_node_data_t *data = (ramfs_node_data_t*)parent->fs_data;
    
    // Find free slot
    int slot = -1;
    for (int i = 0; i < RAMFS_MAX_CHILDREN; i++) {
        if (!data->children[i]) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) return -1; // Directory full
    
    struct vfs_node *new_node = ramfs_alloc_node(name, flags);
    if (!new_node) return -1;
    
    // Link operations
    new_node->finddir = ramfs_finddir;
    new_node->readdir = ramfs_readdir;
    new_node->create = ramfs_create;
    new_node->mkdir = ramfs_mkdir_op; // Use forward declared
    
    data->children[slot] = new_node;
    
    // Copy name to child_names
    int k = 0;
    while(name[k] && k < 31) {
        data->child_names[slot][k] = name[k];
        k++;
    }
    data->child_names[slot][k] = '\0';
    
    return 0;
}

int ramfs_mkdir_op(struct vfs_node *parent, const char *name) {
    return ramfs_create(parent, name, VFS_DIRECTORY);
}

static int ramfs_mount_op(const char *device, struct mount_point *mp) {
    (void)device; 
    
    // Ensure we reset pool on mount? Or just assume it's fresh boot.
    // ramfs_node_count = 0; // Uncomment if we support remounting logic
    
    struct vfs_node *root = ramfs_alloc_node("/", VFS_DIRECTORY);
    
    if (!root) return -1;
    
    root->inode = 0;
    
    // Populate function pointers
    root->readdir = ramfs_readdir;
    root->finddir = ramfs_finddir;
    root->create = ramfs_create;
    root->mkdir = ramfs_mkdir_op;
    
    mp->root = root;
    mp->fs_private = NULL;
    
    return 0;
}

static int ramfs_unmount_op(struct mount_point *mp) {
    return 0;
}

void ramfs_register(void) {
    vfs_register_filesystem("ramfs", ramfs_mount_op, ramfs_unmount_op);
}
