#include "include/syscall.h"
#include "include/autoconf.h"
#include "include/vfs.h"
#include <stdint.h>
#include <stddef.h>

extern void kprintf(const char *format, uint32_t color, ...);

// MSR addresses for SYSCALL/SYSRET
#define MSR_EFER        0xC0000080
#define MSR_STAR        0xC0000081
#define MSR_LSTAR       0xC0000082
#define MSR_SFMASK      0xC0000084

#define EFER_SCE        (1 << 0)  // System Call Extensions

// Read MSR
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

// Write MSR
static inline void wrmsr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

// External syscall entry point (defined in assembly)
extern void syscall_entry(void);

// Current process ID (simple counter for now)
static int current_pid = 1;

// Simple string length
static size_t str_len(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

// Simple string copy
static void str_cpy(char *dst, const char *src) {
    while ((*dst++ = *src++) != '\0');
}

// --- Syscall Implementations ---

static int64_t sys_exit(int status) {
    kprintf("SYSCALL: exit(%d)\n", 0x00FFFF00, status);
    // For now, just halt. Real implementation would terminate the process.
    __asm__ volatile("cli; hlt");
    return 0;
}

static int64_t sys_write(int fd, const char *buf, size_t count) {
    if (!buf || count == 0) return -1;
    
    if (fd == 1 || fd == 2) {  // stdout or stderr
        // Write to console
        extern void fb_putc(char c);
        for (size_t i = 0; i < count; i++) {
            fb_putc(buf[i]);
        }
        return (int64_t)count;
    }
    
    // TODO: VFS file write
    return -1;
}

static int64_t sys_read(int fd, char *buf, size_t count) {
    if (!buf || count == 0) return -1;
    
    if (fd == 0) {  // stdin
        extern int getchar(void);
        for (size_t i = 0; i < count; i++) {
            int c = getchar();
            if (c < 0) return (int64_t)i;
            buf[i] = (char)c;
            if (c == '\n') return (int64_t)(i + 1);
        }
        return (int64_t)count;
    }
    
    // TODO: VFS file read
    return -1;
}

static int64_t sys_open(const char *path, int flags) {
    if (!path) return -1;
    kprintf("SYSCALL: open(\"%s\", %d)\n", 0x00FFFF00, path, flags);
    
    #ifdef CONFIG_VFS
    // TODO: Implement proper file descriptor table
    // For now, just verify the file exists
    struct vfs_node *node = vfs_resolve_path(path);
    if (!node) return -1;
    return 3;  // Return a dummy fd (0,1,2 are std streams)
    #else
    return -1;
    #endif
}

static int64_t sys_close(int fd) {
    if (fd < 3) return -1;  // Can't close std streams
    kprintf("SYSCALL: close(%d)\n", 0x00FFFF00, fd);
    // TODO: Implement proper file descriptor cleanup
    return 0;
}

static int64_t sys_exec(const char *path, char *const argv[]) {
    if (!path) return -1;
    kprintf("SYSCALL: exec(\"%s\")\n", 0x00FFFF00, path);
    
    // TODO: Implement ELF loading and process execution
    // For now, this is a stub
    return -1;
}

static int64_t sys_fork(void) {
    kprintf("SYSCALL: fork()\n", 0x00FFFF00);
    // TODO: Implement process forking
    return -1;
}

static int64_t sys_getpid(void) {
    return current_pid;
}

static int64_t sys_waitpid(int pid, int *status, int options) {
    (void)pid; (void)status; (void)options;
    kprintf("SYSCALL: waitpid(%d)\n", 0x00FFFF00, pid);
    // TODO: Implement process waiting
    return -1;
}

static int64_t sys_sbrk(int64_t increment) {
    (void)increment;
    kprintf("SYSCALL: sbrk(%ld)\n", 0x00FFFF00, increment);
    // TODO: Implement heap expansion
    return -1;
}

static char cwd[256] = "/";

static int64_t sys_getcwd(char *buf, size_t size) {
    if (!buf || size == 0) return -1;
    size_t len = str_len(cwd);
    if (len >= size) return -1;
    str_cpy(buf, cwd);
    return (int64_t)len;
}

static int64_t sys_chdir(const char *path) {
    if (!path) return -1;
    kprintf("SYSCALL: chdir(\"%s\")\n", 0x00FFFF00, path);
    
    #ifdef CONFIG_VFS
    struct vfs_node *node = vfs_resolve_path(path);
    if (!node || !(node->flags & VFS_DIRECTORY)) return -1;
    str_cpy(cwd, path);
    return 0;
    #else
    return -1;
    #endif
}

static int64_t sys_mkdir(const char *path) {
    if (!path) return -1;
    kprintf("SYSCALL: mkdir(\"%s\")\n", 0x00FFFF00, path);
    
    #ifdef CONFIG_VFS
    return vfs_mkdir(path);
    #else
    return -1;
    #endif
}

static int64_t sys_stat(const char *path, void *statbuf) {
    (void)path; (void)statbuf;
    // TODO: Implement stat
    return -1;
}

static int64_t sys_readdir(int fd, void *dirp) {
    (void)fd; (void)dirp;
    // TODO: Implement directory reading
    return -1;
}

// Main syscall dispatcher
int64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2,
                        uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    (void)arg4; (void)arg5;  // Currently unused
    
    switch (num) {
        case SYS_EXIT:
            return sys_exit((int)arg1);
        case SYS_WRITE:
            return sys_write((int)arg1, (const char *)arg2, (size_t)arg3);
        case SYS_READ:
            return sys_read((int)arg1, (char *)arg2, (size_t)arg3);
        case SYS_OPEN:
            return sys_open((const char *)arg1, (int)arg2);
        case SYS_CLOSE:
            return sys_close((int)arg1);
        case SYS_EXEC:
            return sys_exec((const char *)arg1, (char *const *)arg2);
        case SYS_FORK:
            return sys_fork();
        case SYS_GETPID:
            return sys_getpid();
        case SYS_WAITPID:
            return sys_waitpid((int)arg1, (int *)arg2, (int)arg3);
        case SYS_SBRK:
            return sys_sbrk((int64_t)arg1);
        case SYS_GETCWD:
            return sys_getcwd((char *)arg1, (size_t)arg2);
        case SYS_CHDIR:
            return sys_chdir((const char *)arg1);
        case SYS_MKDIR:
            return sys_mkdir((const char *)arg1);
        case SYS_STAT:
            return sys_stat((const char *)arg1, (void *)arg2);
        case SYS_READDIR:
            return sys_readdir((int)arg1, (void *)arg2);
        default:
            kprintf("SYSCALL: Unknown syscall %d\n", 0xFFFF0000, (int)num);
            return -1;
    }
}

void syscall_init(void) {
    kprintf("SYSCALL: Initializing syscall handler...\n", 0x00FF0000);
    
    // Enable SYSCALL/SYSRET instructions
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | EFER_SCE);
    
    // Set up STAR MSR: 
    // Bits 32-47: Kernel CS (0x08 for 64-bit code segment)
    // Bits 48-63: User CS (0x1B for 64-bit user code, assumes GDT layout)
    // Note: User segments are CS=0x1B (ring 3), SS=0x23
    uint64_t star = ((uint64_t)0x08 << 32) | ((uint64_t)0x1B << 48);
    wrmsr(MSR_STAR, star);
    
    // Set LSTAR to syscall entry point
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    
    // Set SFMASK to clear IF (interrupts) during syscall
    wrmsr(MSR_SFMASK, 0x200);  // Clear IF flag
    
    kprintf("SYSCALL: Handler installed at 0x%lx\n", 0x00FF0000, (uint64_t)syscall_entry);
}
