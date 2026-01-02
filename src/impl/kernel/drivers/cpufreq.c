#include "include/cpufreq.h"
#include "include/autoconf.h"
#include <stddef.h>

extern void kprintf(const char *format, uint32_t color, ...);

// MSR addresses for Intel SpeedStep
#define MSR_IA32_PERF_STATUS    0x198
#define MSR_IA32_PERF_CTL       0x199
#define MSR_IA32_MISC_ENABLE    0x1A0

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

static int current_governor = CPUFREQ_GOV_PERFORMANCE;
static int min_freq_mhz = 800;   // Typical minimum
static int max_freq_mhz = 3600;  // Placeholder, should be detected

void cpufreq_init(void) {
    #ifdef CONFIG_CPU_FREQ
    kprintf("CPUFREQ: Initializing CPU frequency scaling...\n", 0x00FF0000);
    
    #ifdef CONFIG_CPU_FREQ_INTEL
    // Check if EIST is enabled
    uint64_t misc_enable = rdmsr(MSR_IA32_MISC_ENABLE);
    if (misc_enable & (1ULL << 16)) {
        kprintf("CPUFREQ: Intel SpeedStep (EIST) detected and enabled\n", 0x00FF0000);
    } else {
        kprintf("CPUFREQ: Intel SpeedStep not available or disabled in BIOS\n", 0xFFFF0000);
    }
    
    // Read current P-state
    uint64_t perf_status = rdmsr(MSR_IA32_PERF_STATUS);
    int current_ratio = (perf_status >> 8) & 0xFF;
    kprintf("CPUFREQ: Current CPU ratio: %d (approx %d MHz)\n", 0x00FFFF00, current_ratio, current_ratio * 100);
    
    max_freq_mhz = current_ratio * 100; // Rough estimate
    #endif
    
    kprintf("CPUFREQ: Initialization complete\n", 0x00FF0000);
    #else
    kprintf("CPUFREQ: CPU frequency scaling disabled in config\n", 0xFFFF0000);
    #endif
}

int cpufreq_get_cur_freq(int cpu) {
    (void)cpu;
    #ifdef CONFIG_CPU_FREQ_INTEL
    uint64_t perf_status = rdmsr(MSR_IA32_PERF_STATUS);
    int ratio = (perf_status >> 8) & 0xFF;
    return ratio * 100 * 1000; // Convert to kHz
    #else
    return 0;
    #endif
}

int cpufreq_get_min_freq(int cpu) {
    (void)cpu;
    return min_freq_mhz * 1000;
}

int cpufreq_get_max_freq(int cpu) {
    (void)cpu;
    return max_freq_mhz * 1000;
}

int cpufreq_set_freq(int cpu, int freq_khz) {
    (void)cpu;
    #ifdef CONFIG_CPU_FREQ_INTEL
    int ratio = freq_khz / 100000; // kHz to ratio (100MHz base)
    if (ratio < 8) ratio = 8; // Minimum ratio
    
    uint64_t perf_ctl = rdmsr(MSR_IA32_PERF_CTL);
    perf_ctl = (perf_ctl & ~0xFF00ULL) | ((uint64_t)ratio << 8);
    wrmsr(MSR_IA32_PERF_CTL, perf_ctl);
    
    kprintf("CPUFREQ: Set CPU ratio to %d\n", 0x00FFFF00, ratio);
    return 0;
    #else
    return -1;
    #endif
}

int cpufreq_set_governor(int governor) {
    current_governor = governor;
    switch (governor) {
        case CPUFREQ_GOV_PERFORMANCE:
            kprintf("CPUFREQ: Governor set to 'performance'\n", 0x00FFFF00);
            cpufreq_set_freq(0, max_freq_mhz * 1000);
            break;
        case CPUFREQ_GOV_POWERSAVE:
            kprintf("CPUFREQ: Governor set to 'powersave'\n", 0x00FFFF00);
            cpufreq_set_freq(0, min_freq_mhz * 1000);
            break;
        default:
            kprintf("CPUFREQ: Governor set to 'ondemand'\n", 0x00FFFF00);
            break;
    }
    return 0;
}
