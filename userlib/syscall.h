#ifndef _SYSCALL_H
#define _SYSCALL_H

// Syscall numbers (must match kernel/include/syscall.h)
#define SYS_EXIT        0
#define SYS_WRITE       1
#define SYS_READ        2
#define SYS_OPEN        3
#define SYS_CLOSE       4
#define SYS_EXEC        5
#define SYS_FORK        6
#define SYS_GETPID      7
#define SYS_WAITPID     8
#define SYS_SBRK        9
#define SYS_GETCWD      10
#define SYS_CHDIR       11
#define SYS_MKDIR       12
#define SYS_STAT        13
#define SYS_READDIR     14

typedef unsigned long size_t;
typedef long ssize_t;
typedef long pid_t;

// Raw syscall interface
static inline long syscall0(long num) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall1(long num, long arg1) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall2(long num, long arg1, long arg2) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall3(long num, long arg1, long arg2, long arg3) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall4(long num, long arg1, long arg2, long arg3, long arg4) {
    long ret;
    register long r10 __asm__("r10") = arg4;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// System call wrappers
static inline void exit(int status) {
    syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}

static inline ssize_t write(int fd, const void *buf, size_t count) {
    return syscall3(SYS_WRITE, fd, (long)buf, count);
}

static inline ssize_t read(int fd, void *buf, size_t count) {
    return syscall3(SYS_READ, fd, (long)buf, count);
}

static inline int open(const char *path, int flags) {
    return (int)syscall2(SYS_OPEN, (long)path, flags);
}

static inline int close(int fd) {
    return (int)syscall1(SYS_CLOSE, fd);
}

static inline int exec(const char *path, char *const argv[]) {
    return (int)syscall2(SYS_EXEC, (long)path, (long)argv);
}

static inline pid_t fork(void) {
    return (pid_t)syscall0(SYS_FORK);
}

static inline pid_t getpid(void) {
    return (pid_t)syscall0(SYS_GETPID);
}

static inline pid_t waitpid(pid_t pid, int *status, int options) {
    return (pid_t)syscall3(SYS_WAITPID, pid, (long)status, options);
}

static inline void *sbrk(long increment) {
    return (void *)syscall1(SYS_SBRK, increment);
}

static inline char *getcwd(char *buf, size_t size) {
    long ret = syscall2(SYS_GETCWD, (long)buf, size);
    return ret >= 0 ? buf : (void *)0;
}

static inline int chdir(const char *path) {
    return (int)syscall1(SYS_CHDIR, (long)path);
}

static inline int mkdir(const char *path) {
    return (int)syscall1(SYS_MKDIR, (long)path);
}

#endif // _SYSCALL_H
