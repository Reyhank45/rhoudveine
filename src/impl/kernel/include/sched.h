#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>

// Task states
#define TASK_RUNNING    0
#define TASK_READY      1
#define TASK_BLOCKED    2
#define TASK_ZOMBIE     3

// Maximum CPUs and tasks
#define MAX_CPUS        64
#define MAX_TASKS       256

// Task structure
typedef struct task {
    uint32_t id;
    uint32_t state;
    uint32_t cpu_id;        // CPU this task is assigned to
    uint64_t time_slice;    // Remaining time slice in ticks
    uint64_t total_runtime; // Total runtime in ticks
    void *stack;
    void *context;
    struct task *next;      // Next task in run queue
    char name[64];
} task_t;

// CPU structure
typedef struct cpu_info {
    uint32_t id;
    uint32_t apic_id;
    uint32_t is_bsp;        // Bootstrap processor?
    uint32_t is_smt;        // Is this a hyperthread?
    uint32_t core_id;       // Physical core ID (for SMT detection)
    uint32_t package_id;    // Physical package/socket
    uint32_t online;
    task_t *current;        // Currently running task
    task_t *run_queue;      // Head of run queue
    uint64_t idle_time;
} cpu_info_t;

// Scheduler API
void sched_init(void);
int sched_cpu_count(void);
cpu_info_t *sched_get_cpu(int id);
task_t *sched_create_task(const char *name, void (*entry)(void));
void sched_yield(void);
void sched_schedule(void);
void sched_tick(void);

// SMT-aware scheduling
void sched_set_smt_aware(int enabled);

#endif // SCHED_H
