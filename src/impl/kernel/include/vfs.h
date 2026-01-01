#ifndef KERNEL_VFS_H
#define KERNEL_VFS_H

#include <stdint.h>
#include <stddef.h>

// VFS node types
#define VFS_FILE        0x01
#define VFS_DIRECTORY   0x02
#define VFS_CHARDEVICE  0x04
#define VFS_BLOCKDEVICE 0x08
#define VFS_PIPE        0x10
#define VFS_SYMLINK     0x20
#define VFS_MOUNTPOINT  0x40

// File open flags
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0100
#define O_TRUNC     0x0200
#define O_APPEND    0x0400

// Seek modes
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

// Forward declarations
struct vfs_node;
struct mount_point;

// VFS node operations
typedef int (*vfs_open_t)(struct vfs_node *node, uint32_t flags);
typedef void (*vfs_close_t)(struct vfs_node *node);
typedef int (*vfs_read_t)(struct vfs_node *node, uint64_t offset, uint32_t size, uint8_t *buffer);
typedef int (*vfs_write_t)(struct vfs_node *node, uint64_t offset, uint32_t size, const uint8_t *buffer);
typedef struct vfs_node* (*vfs_readdir_t)(struct vfs_node *node, uint32_t index);
typedef struct vfs_node* (*vfs_finddir_t)(struct vfs_node *node, const char *name);
typedef int (*vfs_create_t)(struct vfs_node *parent, const char *name, uint32_t flags);
typedef int (*vfs_unlink_t)(struct vfs_node *parent, const char *name);
typedef int (*vfs_mkdir_t)(struct vfs_node *parent, const char *name);

// VFS node structure
struct vfs_node {
    char name[256];
    uint32_t inode;
    uint32_t size;
    uint32_t flags;
    uint32_t permissions;
    uint32_t uid;
    uint32_t gid;
    
    // Operations
    vfs_open_t open;
    vfs_close_t close;
    vfs_read_t read;
    vfs_write_t write;
    vfs_readdir_t readdir;
    vfs_finddir_t finddir;
    vfs_create_t create;
    vfs_unlink_t unlink;
    vfs_mkdir_t mkdir;
    
    // Filesystem-specific data
    void *fs_data;
    
    // Mount point reference (if this is a mount point)
    struct mount_point *mount;
};

// Mount point structure
struct mount_point {
    char path[256];
    struct vfs_node *root;
    void *fs_private;
    int refcount;
};

// Directory entry (for readdir)
struct dirent {
    uint32_t inode;
    char name[256];
    uint8_t type;
};

// File descriptor
struct file_descriptor {
    struct vfs_node *node;
    uint64_t offset;
    uint32_t flags;
    int refcount;
};

// Filesystem type registration
typedef int (*fs_mount_t)(const char *device, struct mount_point *mp);
typedef int (*fs_unmount_t)(struct mount_point *mp);

struct filesystem_type {
    char name[32];
    fs_mount_t mount;
    fs_unmount_t unmount;
};

// VFS initialization
void vfs_init(void);

// Filesystem registration
int vfs_register_filesystem(const char *name, fs_mount_t mount, fs_unmount_t unmount);

// Mount operations
int vfs_mount(const char *path, const char *fstype, const char *device);
int vfs_unmount(const char *path);

// File operations
int vfs_open(const char *path, uint32_t flags);
int vfs_close(int fd);
int vfs_read(int fd, void *buffer, size_t count);
int vfs_write(int fd, const void *buffer, size_t count);
int vfs_seek(int fd, int64_t offset, int whence);
uint64_t vfs_tell(int fd);

// Directory operations
int vfs_readdir(int fd, struct dirent *entry);

// File management
int vfs_create(const char *path);
int vfs_unlink(const char *path);
int vfs_mkdir(const char *path);

// Path resolution
struct vfs_node* vfs_resolve_path(const char *path);

// Utility functions
const char* vfs_basename(const char *path);
const char* vfs_dirname(const char *path, char *out, size_t out_size);

#endif // KERNEL_VFS_H
