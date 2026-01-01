#include "include/vfs.h"
#include "stdio.h"
#include <stdint.h>
#include <stddef.h>

extern void kprintf(const char *format, uint32_t color, ...);

// String functions
static size_t my_strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
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

static char* my_strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

static char* my_strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

// Mount table
#define MAX_MOUNTS 16
static struct mount_point g_mounts[MAX_MOUNTS];
static int g_mount_count = 0;

// File descriptor table
#define MAX_FDS 256
static struct file_descriptor g_fds[MAX_FDS];

// Filesystem type registry
#define MAX_FS_TYPES 16
static struct filesystem_type g_fs_types[MAX_FS_TYPES];
static int g_fs_type_count = 0;

// Root VFS node
static struct vfs_node *g_vfs_root = NULL;

void vfs_init(void) {
    kprintf("VFS: Initializing Virtual File System...\n", 0x00FF0000);
    
    // Clear tables
    for (int i = 0; i < MAX_MOUNTS; i++) {
        g_mounts[i].path[0] = '\0';
        g_mounts[i].root = NULL;
        g_mounts[i].fs_private = NULL;
        g_mounts[i].refcount = 0;
    }
    
    for (int i = 0; i < MAX_FDS; i++) {
        g_fds[i].node = NULL;
        g_fds[i].offset = 0;
        g_fds[i].flags = 0;
        g_fds[i].refcount = 0;
    }
    
    for (int i = 0; i < MAX_FS_TYPES; i++) {
        g_fs_types[i].name[0] = '\0';
        g_fs_types[i].mount = NULL;
        g_fs_types[i].unmount = NULL;
    }
    
    g_mount_count = 0;
    g_fs_type_count = 0;
    g_vfs_root = NULL;
    
    kprintf("VFS: Initialization complete\n", 0x00FF0000);
}

int vfs_register_filesystem(const char *name, fs_mount_t mount, fs_unmount_t unmount) {
    if (g_fs_type_count >= MAX_FS_TYPES) {
        kprintf("VFS: Filesystem registry full\n", 0xFFFF0000);
        return -1;
    }
    
    my_strncpy(g_fs_types[g_fs_type_count].name, name, 31);
    g_fs_types[g_fs_type_count].name[31] = '\0';
    g_fs_types[g_fs_type_count].mount = mount;
    g_fs_types[g_fs_type_count].unmount = unmount;
    
    kprintf("VFS: Registered filesystem type '%s'\n", 0x00FF0000, name);
    g_fs_type_count++;
    return 0;
}

static struct filesystem_type* find_fs_type(const char *name) {
    for (int i = 0; i < g_fs_type_count; i++) {
        if (my_strcmp(g_fs_types[i].name, name) == 0) {
            return &g_fs_types[i];
        }
    }
    return NULL;
}

int vfs_mount(const char *path, const char *fstype, const char *device) {
    if (g_mount_count >= MAX_MOUNTS) {
        kprintf("VFS: Mount table full\n", 0xFFFF0000);
        return -1;
    }
    
    struct filesystem_type *fs = find_fs_type(fstype);
    if (!fs) {
        kprintf("VFS: Unknown filesystem type '%s'\n", 0xFFFF0000, fstype);
        return -1;
    }
    
    struct mount_point *mp = &g_mounts[g_mount_count];
    my_strncpy(mp->path, path, 255);
    mp->path[255] = '\0';
    mp->root = NULL;
    mp->fs_private = NULL;
    mp->refcount = 0;
    
    kprintf("VFS: Mounting '%s' at '%s' (type: %s)\n", 0x00FF0000, device, path, fstype);
    
    if (fs->mount(device, mp) != 0) {
        kprintf("VFS: Mount failed\n", 0xFFFF0000);
        return -1;
    }
    
    // If mounting at root, set global root
    if (my_strcmp(path, "/") == 0) {
        g_vfs_root = mp->root;
        kprintf("VFS: Root filesystem mounted\n", 0x00FF0000);
    }
    
    g_mount_count++;
    kprintf("VFS: Mount successful\n", 0x00FF0000);
    return 0;
}

int vfs_unmount(const char *path) {
    for (int i = 0; i < g_mount_count; i++) {
        if (my_strcmp(g_mounts[i].path, path) == 0) {
            if (g_mounts[i].refcount > 0) {
                kprintf("VFS: Cannot unmount, filesystem is busy\n", 0xFFFF0000);
                return -1;
            }
            
            // TODO: Call filesystem unmount callback
            
            // Shift remaining mounts
            for (int j = i; j < g_mount_count - 1; j++) {
                g_mounts[j] = g_mounts[j + 1];
            }
            g_mount_count--;
            
            kprintf("VFS: Unmounted '%s'\n", 0x00FF0000, path);
            return 0;
        }
    }
    
    kprintf("VFS: Mount point '%s' not found\n", 0xFFFF0000, path);
    return -1;
}

// Path utility functions
const char* vfs_basename(const char *path) {
    const char *last_slash = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') last_slash = p + 1;
    }
    return last_slash;
}

const char* vfs_dirname(const char *path, char *out, size_t out_size) {
    const char *last_slash = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }
    
    if (!last_slash) {
        my_strncpy(out, ".", out_size);
        return out;
    }
    
    size_t len = last_slash - path;
    if (len == 0) len = 1; // Root directory
    if (len >= out_size) len = out_size - 1;
    
    my_strncpy(out, path, len);
    out[len] = '\0';
    return out;
}

struct vfs_node* vfs_resolve_path(const char *path) {
    if (!g_vfs_root) {
        kprintf("VFS: No root filesystem mounted\n", 0xFFFF0000);
        return NULL;
    }
    
    if (!path || path[0] != '/') {
        kprintf("VFS: Invalid path (must start with /)\n", 0xFFFF0000);
        return NULL;
    }
    
    // Special case: root directory
    if (path[1] == '\0') {
        return g_vfs_root;
    }
    
    struct vfs_node *current = g_vfs_root;
    char component[256];
    const char *p = path + 1; // Skip leading /
    
    while (*p) {
        // Extract next path component
        int i = 0;
        while (*p && *p != '/' && i < 255) {
            component[i++] = *p++;
        }
        component[i] = '\0';
        
        if (*p == '/') p++; // Skip trailing slash
        
        if (i == 0) continue; // Empty component (double slash)
        
        // Look up component in current directory
        if (!current->finddir) {
            kprintf("VFS: Not a directory\n", 0xFFFF0000);
            return NULL;
        }
        
        struct vfs_node *next = current->finddir(current, component);
        if (!next) {
            return NULL; // Component not found
        }
        
        current = next;
        
        // Check if we hit a mount point
        if (current->flags & VFS_MOUNTPOINT && current->mount) {
            current = current->mount->root;
        }
    }
    
    return current;
}

int vfs_open(const char *path, uint32_t flags) {
    struct vfs_node *node = vfs_resolve_path(path);
    
    // If node doesn't exist and O_CREAT is set, try to create it
    if (!node && (flags & O_CREAT)) {
        if (vfs_create(path) != 0) {
            return -1;
        }
        node = vfs_resolve_path(path);
    }
    
    if (!node) {
        kprintf("VFS: File not found: %s\n", 0xFFFF0000, path);
        return -1;
    }
    
    // Find free FD
    int fd = -1;
    for (int i = 3; i < MAX_FDS; i++) { // Reserve 0,1,2 for stdin/stdout/stderr
        if (g_fds[i].refcount == 0) {
            fd = i;
            break;
        }
    }
    
    if (fd == -1) {
        kprintf("VFS: No free file descriptors\n", 0xFFFF0000);
        return -1;
    }
    
    // Call filesystem-specific open
    if (node->open && node->open(node, flags) != 0) {
        kprintf("VFS: Open failed\n", 0xFFFF0000);
        return -1;
    }
    
    g_fds[fd].node = node;
    g_fds[fd].offset = (flags & O_APPEND) ? node->size : 0;
    g_fds[fd].flags = flags;
    g_fds[fd].refcount = 1;
    
    return fd;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= MAX_FDS || g_fds[fd].refcount == 0) {
        return -1;
    }
    
    g_fds[fd].refcount--;
    if (g_fds[fd].refcount == 0) {
        if (g_fds[fd].node && g_fds[fd].node->close) {
            g_fds[fd].node->close(g_fds[fd].node);
        }
        g_fds[fd].node = NULL;
    }
    
    return 0;
}

int vfs_read(int fd, void *buffer, size_t count) {
    if (fd < 0 || fd >= MAX_FDS || g_fds[fd].refcount == 0) {
        return -1;
    }
    
    struct file_descriptor *file = &g_fds[fd];
    if (!file->node || !file->node->read) {
        return -1;
    }
    
    int bytes_read = file->node->read(file->node, file->offset, count, (uint8_t*)buffer);
    if (bytes_read > 0) {
        file->offset += bytes_read;
    }
    
    return bytes_read;
}

int vfs_write(int fd, const void *buffer, size_t count) {
    if (fd < 0 || fd >= MAX_FDS || g_fds[fd].refcount == 0) {
        return -1;
    }
    
    struct file_descriptor *file = &g_fds[fd];
    if (!file->node || !file->node->write) {
        return -1;
    }
    
    int bytes_written = file->node->write(file->node, file->offset, count, (const uint8_t*)buffer);
    if (bytes_written > 0) {
        file->offset += bytes_written;
    }
    
    return bytes_written;
}

int vfs_seek(int fd, int64_t offset, int whence) {
    if (fd < 0 || fd >= MAX_FDS || g_fds[fd].refcount == 0) {
        return -1;
    }
    
    struct file_descriptor *file = &g_fds[fd];
    
    switch (whence) {
        case SEEK_SET:
            file->offset = offset;
            break;
        case SEEK_CUR:
            file->offset += offset;
            break;
        case SEEK_END:
            if (file->node) {
                file->offset = file->node->size + offset;
            }
            break;
        default:
            return -1;
    }
    
    return 0;
}

uint64_t vfs_tell(int fd) {
    if (fd < 0 || fd >= MAX_FDS || g_fds[fd].refcount == 0) {
        return (uint64_t)-1;
    }
    return g_fds[fd].offset;
}

int vfs_readdir(int fd, struct dirent *entry) {
    if (fd < 0 || fd >= MAX_FDS || g_fds[fd].refcount == 0) {
        return -1;
    }
    
    struct file_descriptor *file = &g_fds[fd];
    if (!file->node || !file->node->readdir) {
        return -1;
    }
    
    struct vfs_node *child = file->node->readdir(file->node, file->offset);
    if (!child) {
        return -1; // End of directory
    }
    
    entry->inode = child->inode;
    my_strncpy(entry->name, child->name, 255);
    entry->name[255] = '\0';
    entry->type = child->flags;
    
    file->offset++;
    return 0;
}

int vfs_create(const char *path) {
    char dirname[256];
    vfs_dirname(path, dirname, sizeof(dirname));
    
    struct vfs_node *parent = vfs_resolve_path(dirname);
    if (!parent) {
        kprintf("VFS: Parent directory not found\n", 0xFFFF0000);
        return -1;
    }
    
    const char *filename = vfs_basename(path);
    
    if (!parent->create) {
        kprintf("VFS: Filesystem does not support file creation\n", 0xFFFF0000);
        return -1;
    }
    
    return parent->create(parent, filename, VFS_FILE);
}

int vfs_unlink(const char *path) {
    char dirname[256];
    vfs_dirname(path, dirname, sizeof(dirname));
    
    struct vfs_node *parent = vfs_resolve_path(dirname);
    if (!parent) {
        return -1;
    }
    
    const char *filename = vfs_basename(path);
    
    if (!parent->unlink) {
        return -1;
    }
    
    return parent->unlink(parent, filename);
}

int vfs_mkdir(const char *path) {
    char dirname[256];
    vfs_dirname(path, dirname, sizeof(dirname));
    
    struct vfs_node *parent = vfs_resolve_path(dirname);
    if (!parent) {
        return -1;
    }
    
    const char *filename = vfs_basename(path);
    
    if (!parent->mkdir) {
        return -1;
    }
    
    return parent->mkdir(parent, filename);
}
