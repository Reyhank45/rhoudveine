#include "include/acpi.h"
#include "stdio.h"
#include <stdint.h>
#include <stddef.h>

// Forward declaration for kprintf
extern void kprintf(const char *format, uint32_t color, ...);

// Custom implementation of memcmp
static int custom_memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = s1;
    const unsigned char *p2 = s2;
    while (n-- > 0) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

static int acpi_check_rsdp(uint8_t *ptr) {
    if (custom_memcmp(ptr, "RSD PTR ", 8) != 0) {
        return 0;
    }

    uint8_t sum = 0;
    for (int i = 0; i < 20; i++) {
        sum += ptr[i];
    }

    return sum == 0;
}

static struct rsdp_descriptor *find_rsdp() {
    // Search in the first 1KB of the EBDA
    // For now, we will skip this part as getting the EBDA address is complex.

    // Search in the BIOS ROM area
    for (uint8_t *ptr = (uint8_t *)0xE0000; ptr < (uint8_t *)0xFFFFF; ptr += 16) {
        if (acpi_check_rsdp(ptr)) {
            return (struct rsdp_descriptor *)ptr;
        }
    }

    return NULL;
}

void acpi_init() {
    struct rsdp_descriptor *rsdp = find_rsdp();
    if (rsdp) {
        kprintf("ACPI: Found RSDP at 0x%lx\n", 0x00FF0000, (uint64_t)rsdp);
    } else {
        kprintf("ACPI: RSDP not found.\n", 0x00FF0000);
    }
}
