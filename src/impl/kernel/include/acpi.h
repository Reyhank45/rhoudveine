#ifndef KERNEL_ACPI_H
#define KERNEL_ACPI_H

#include <stdint.h>

// Root System Description Pointer
struct rsdp_descriptor {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed));

struct rsdp_descriptor_2 {
    struct rsdp_descriptor first_part;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

void acpi_init();

#endif // KERNEL_ACPI_H