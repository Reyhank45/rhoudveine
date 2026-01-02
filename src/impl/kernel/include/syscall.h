#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include <stddef.h>

// System call numbers
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
#define SYS_MAX         15

// Initialize syscall handler (setup MSRs for SYSCALL instruction)
void syscall_init(void);

// Syscall handler (called from assembly)
int64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, 
                        uint64_t arg3, uint64_t arg4, uint64_t arg5);

#endif // SYSCALL_H
