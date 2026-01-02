#ifndef CPUFREQ_H
#define CPUFREQ_H

#include <stdint.h>

// CPU frequency governors
#define CPUFREQ_GOV_PERFORMANCE 0
#define CPUFREQ_GOV_POWERSAVE   1
#define CPUFREQ_GOV_ONDEMAND    2

// P-State structure
typedef struct {
    uint32_t frequency;   // kHz
    uint32_t power;       // mW
    uint32_t latency;     // us
} pstate_t;

// CPU frequency API
void cpufreq_init(void);
int cpufreq_get_cur_freq(int cpu);      // Returns current frequency in kHz
int cpufreq_get_min_freq(int cpu);
int cpufreq_get_max_freq(int cpu);
int cpufreq_set_freq(int cpu, int freq_khz);
int cpufreq_set_governor(int governor);

#endif // CPUFREQ_H
