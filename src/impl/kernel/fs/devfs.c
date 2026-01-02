#include "include/vfs.h"
#include "include/vnode.h"
#include "include/stdio.h"
#include <stddef.h>

extern void kprintf(const char *format, uint32_t color, ...);

// Basic DevFS implementation
#define MAX_DEVFS_NODES 16
static struct vfs_node devfs_root;
static struct vfs_node devfs_nodes[MAX_DEVFS_NODES];
static int devfs_node_count = 0;
// We need an array of pointers for the root directory to list
static struct vfs_node *devfs_children[MAX_DEVFS_NODES];

static struct vfs_node* devfs_finddir(struct vfs_node *node, const char *name) {
    (void)node; // Assume root
    for (int i = 0; i < devfs_node_count; i++) {
        // Simple manual strcmp
        const char *n = devfs_children[i]->name;
        const char *t = name;
        int match = 1;
        while(*n && *t) { if (*n != *t) { match = 0; break; } n++; t++; }
        if (match && *n == *t) return devfs_children[i];
    }
    return NULL;
}

static struct vfs_node* devfs_readdir(struct vfs_node *node, uint32_t index) {
    (void)node; // Assume root
    if (index >= (uint32_t)devfs_node_count) return NULL;
    return devfs_children[index];
}

static int devfs_mount_op(const char *device, struct mount_point *mp) {
    (void)device;
    devfs_root.flags = VFS_DIRECTORY;
    devfs_root.fs_data = NULL; // We use static global arrays instead of fs_data for simplicity here
    devfs_root.finddir = devfs_finddir;
    devfs_root.readdir = devfs_readdir;
    mp->root = &devfs_root;
    return 0;
}

static int devfs_unmount_op(struct mount_point *mp) {
    (void)mp;
    return 0;
}

// Add device entry
void devfs_add_device(const char *name, void *device_data) {
    if (devfs_node_count >= MAX_DEVFS_NODES) return;
    
    struct vfs_node *node = &devfs_nodes[devfs_node_count];
    
    // Copy name
    int i = 0;
    while(name[i] && i < 31) {
        node->name[i] = name[i];
        i++;
    }
    node->name[i] = '\0';
    
    node->flags = VFS_FILE; // Devices are files (block/char)
    node->fs_data = device_data;
    node->size = 0; 
    
    devfs_children[devfs_node_count] = node;
    devfs_node_count++;
}

void devfs_register(void) {
    vfs_register_filesystem("DeviceFS", devfs_mount_op, devfs_unmount_op);
}
