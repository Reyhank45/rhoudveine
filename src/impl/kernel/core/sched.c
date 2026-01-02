#include "include/sched.h"
#include "include/autoconf.h"
#include <stddef.h>

extern void kprintf(const char *format, uint32_t color, ...);

// CPU and task pools
static cpu_info_t cpus[MAX_CPUS];
static int cpu_count = 0;

static task_t task_pool[MAX_TASKS];
static int next_task_id = 0;

static int smt_aware = 1;

// Idle task for each CPU
static task_t idle_tasks[MAX_CPUS];

// Custom memset
static void *sched_memset(void *s, int c, uint32_t n) {
    unsigned char *p = s;
    while (n-- > 0) *p++ = (unsigned char)c;
    return s;
}

// Custom strcpy
static void sched_strcpy(char *dest, const char *src) {
    while ((*dest++ = *src++) != '\0');
}

// Detect CPUs from ACPI MADT (we already parsed this in acpi.c)
// For now, use extern declarations to get the count
extern int acpi_cpu_count;
extern uint32_t acpi_cpu_apic_ids[];

void sched_init(void) {
    #ifdef CONFIG_SMP
    kprintf("SCHED: Initializing multi-core scheduler...\n", 0x00FF0000);
    
    // Initialize CPU structures
    sched_memset(cpus, 0, sizeof(cpus));
    sched_memset(task_pool, 0, sizeof(task_pool));
    
    // Get CPU info from ACPI
    cpu_count = acpi_cpu_count > 0 ? acpi_cpu_count : 1;
    if (cpu_count > MAX_CPUS) cpu_count = MAX_CPUS;
    
    for (int i = 0; i < cpu_count; i++) {
        cpus[i].id = i;
        cpus[i].apic_id = acpi_cpu_apic_ids[i];
        cpus[i].is_bsp = (i == 0);
        cpus[i].online = (i == 0); // Only BSP is online initially
        cpus[i].run_queue = NULL;
        cpus[i].current = NULL;
        
        // SMT detection: Assume hyperthreads have odd APIC IDs (simplified)
        // Real detection uses CPUID leaf 0x0B
        #ifdef CONFIG_SMT_SCHED
        cpus[i].is_smt = (cpus[i].apic_id & 1) && (cpu_count > 1);
        cpus[i].core_id = cpus[i].apic_id >> 1;
        #else
        cpus[i].is_smt = 0;
        cpus[i].core_id = i;
        #endif
        
        cpus[i].package_id = 0; // Single socket assumed
        
        // Create idle task for this CPU
        idle_tasks[i].id = 0xFFFF0000 | i;
        idle_tasks[i].state = TASK_READY;
        idle_tasks[i].cpu_id = i;
        sched_strcpy(idle_tasks[i].name, "idle");
    }
    
    kprintf("SCHED: Detected %d CPU(s)\n", 0x00FF0000, cpu_count);
    
    #ifdef CONFIG_SMT_SCHED
    int physical_cores = 0;
    for (int i = 0; i < cpu_count; i++) {
        if (!cpus[i].is_smt) physical_cores++;
    }
    kprintf("SCHED: SMT-aware scheduling enabled (%d physical cores)\n", 0x00FF0000, physical_cores);
    #endif
    
    #else
    kprintf("SCHED: SMP disabled, using single CPU\n", 0x00FF0000);
    cpu_count = 1;
    cpus[0].id = 0;
    cpus[0].is_bsp = 1;
    cpus[0].online = 1;
    #endif
}

int sched_cpu_count(void) {
    return cpu_count;
}

cpu_info_t *sched_get_cpu(int id) {
    if (id < 0 || id >= cpu_count) return NULL;
    return &cpus[id];
}

task_t *sched_create_task(const char *name, void (*entry)(void)) {
    if (next_task_id >= MAX_TASKS) return NULL;
    
    task_t *t = &task_pool[next_task_id++];
    t->id = next_task_id;
    t->state = TASK_READY;
    t->time_slice = 10; // Default time slice
    t->total_runtime = 0;
    sched_strcpy(t->name, name);
    
    // Assign to least loaded CPU
    int target_cpu = 0;
    int min_load = 0x7FFFFFFF;
    
    for (int i = 0; i < cpu_count; i++) {
        if (!cpus[i].online) continue;
        
        #ifdef CONFIG_SMT_SCHED
        // Prefer physical cores over hyperthreads if smt_aware
        if (smt_aware && cpus[i].is_smt) continue;
        #endif
        
        // Count tasks in run queue
        int load = 0;
        task_t *q = cpus[i].run_queue;
        while (q) { load++; q = q->next; }
        
        if (load < min_load) {
            min_load = load;
            target_cpu = i;
        }
    }
    
    t->cpu_id = target_cpu;
    
    // Add to run queue
    t->next = cpus[target_cpu].run_queue;
    cpus[target_cpu].run_queue = t;
    
    kprintf("SCHED: Created task '%s' (ID %d) on CPU %d\n", 0x00FFFF00, name, t->id, target_cpu);
    
    return t;
}

void sched_yield(void) {
    // Placeholder: In real implementation, this would save context and call schedule
    sched_schedule();
}

void sched_schedule(void) {
    // Simple round-robin on BSP for now
    cpu_info_t *cpu = &cpus[0];
    
    if (!cpu->run_queue) {
        cpu->current = &idle_tasks[0];
        return;
    }
    
    // Round-robin: Move current to end, pick next
    task_t *next = cpu->run_queue;
    if (next->state == TASK_READY) {
        cpu->current = next;
        cpu->run_queue = next->next;
        next->next = NULL;
        
        // Add back to end of queue
        if (cpu->run_queue) {
            task_t *tail = cpu->run_queue;
            while (tail->next) tail = tail->next;
            tail->next = next;
        } else {
            cpu->run_queue = next;
        }
    }
}

void sched_tick(void) {
    // Called from timer interrupt
    for (int i = 0; i < cpu_count; i++) {
        if (!cpus[i].online) continue;
        
        task_t *current = cpus[i].current;
        if (current && current->time_slice > 0) {
            current->time_slice--;
            current->total_runtime++;
            
            if (current->time_slice == 0) {
                current->time_slice = 10; // Reset slice
                // Trigger reschedule (simplified)
            }
        }
    }
}

void sched_set_smt_aware(int enabled) {
    smt_aware = enabled;
    kprintf("SCHED: SMT-aware scheduling %s\n", 0x00FFFF00, enabled ? "enabled" : "disabled");
}
