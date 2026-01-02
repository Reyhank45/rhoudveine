#include "include/vfs.h"
#include "include/stdio.h"
#include <stddef.h>

extern void kprintf(const char *format, uint32_t color, ...);

// Basic ProcFS implementation
#define MAX_PROCFS_NODES 16
static struct vfs_node procfs_root;
static struct vfs_node procfs_nodes[MAX_PROCFS_NODES];
static char procfs_content[MAX_PROCFS_NODES][256]; // Store small text content
static int procfs_node_count = 0;
static struct vfs_node *procfs_children[MAX_PROCFS_NODES];

static struct vfs_node* procfs_finddir(struct vfs_node *node, const char *name) {
    (void)node;
    for (int i = 0; i < procfs_node_count; i++) {
        const char *n = procfs_children[i]->name;
        const char *t = name;
        int match = 1;
        while(*n && *t) { if (*n != *t) { match = 0; break; } n++; t++; }
        if (match && *n == *t) return procfs_children[i];
    }
    return NULL;
}

static struct vfs_node* procfs_readdir(struct vfs_node *node, uint32_t index) {
    (void)node;
    if (index >= (uint32_t)procfs_node_count) return NULL;
    return procfs_children[index];
}

static int procfs_read(struct vfs_node *node, uint64_t offset, uint32_t count, uint8_t *buffer) {
    if (!node->fs_data) return 0;
    const char *content = (const char*)node->fs_data;
    
    // Simple strlen
    int len = 0;
    while(content[len]) len++;
    
    if (offset >= (uint64_t)len) return 0;
    
    int read_len = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (offset + i >= (uint64_t)len) break;
        buffer[i] = content[offset + i];
        read_len++;
    }
    return read_len;
}

static int procfs_mount_op(const char *device, struct mount_point *mp) {
    (void)device;
    procfs_root.flags = VFS_DIRECTORY;
    procfs_root.fs_data = NULL;
    procfs_root.finddir = procfs_finddir;
    procfs_root.readdir = procfs_readdir;
    mp->root = &procfs_root;
    return 0;
}

static int procfs_unmount_op(struct mount_point *mp) {
    (void)mp;
    return 0;
}

// Add process entry
void procfs_add_entry(const char *name, const char *content) {
    if (procfs_node_count >= MAX_PROCFS_NODES) return;
    
    struct vfs_node *node = &procfs_nodes[procfs_node_count];
    char *store = procfs_content[procfs_node_count];
    
    // Copy name
    int i = 0;
    while(name[i] && i < 31) {
        node->name[i] = name[i];
        i++;
    }
    node->name[i] = '\0';
    
    // Copy content
    i = 0;
    while(content[i] && i < 255) {
        store[i] = content[i];
        i++;
    }
    store[i] = '\0';
    
    node->flags = VFS_FILE;
    node->fs_data = (void*)store;
    node->size = i;
    node->read = procfs_read;
    
    procfs_children[procfs_node_count] = node;
    procfs_node_count++;
}

void procfs_register(void) {
    vfs_register_filesystem("ProcessFS", procfs_mount_op, procfs_unmount_op);
}
