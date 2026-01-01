#ifndef KERNEL_ACPI_H
#define KERNEL_ACPI_H

#include <stdint.h>

// --------------------------------------------------------------------------
// ACPI Table Structures
// --------------------------------------------------------------------------

// Root System Description Pointer (ACPI 1.0)
struct rsdp_descriptor {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed));

// Extended RSDP (ACPI 2.0+)
struct rsdp_descriptor_2 {
    struct rsdp_descriptor first_part;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

// Standard ACPI table header
struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

// Root System Description Table (ACPI 1.0)
struct rsdt {
    struct acpi_sdt_header header;
    uint32_t sdt_pointers[]; // Variable length
} __attribute__((packed));

// Extended System Description Table (ACPI 2.0+)
struct xsdt {
    struct acpi_sdt_header header;
    uint64_t sdt_pointers[]; // Variable length
} __attribute__((packed));

// Fixed ACPI Description Table (FADT)
struct fadt {
    struct acpi_sdt_header header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    
    // ACPI 1.0 fields
    uint8_t reserved;
    uint8_t preferred_pm_profile;
    uint16_t sci_interrupt;
    uint32_t smi_command_port;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_control;
    uint32_t pm1a_event_block;
    uint32_t pm1b_event_block;
    uint32_t pm1a_control_block;
    uint32_t pm1b_control_block;
    uint32_t pm2_control_block;
    uint32_t pm_timer_block;
    uint32_t gpe0_block;
    uint32_t gpe1_block;
    uint8_t pm1_event_length;
    uint8_t pm1_control_length;
    uint8_t pm2_control_length;
    uint8_t pm_timer_length;
    uint8_t gpe0_length;
    uint8_t gpe1_length;
    uint8_t gpe1_base;
    uint8_t cstate_control;
    uint16_t worst_c2_latency;
    uint16_t worst_c3_latency;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alarm;
    uint8_t month_alarm;
    uint8_t century;
    
    // ACPI 2.0+ fields
    uint16_t boot_arch_flags;
    uint8_t reserved2;
    uint32_t flags;
    // ... more fields exist but these are the critical ones
} __attribute__((packed));

// Multiple APIC Description Table (MADT)
struct madt {
    struct acpi_sdt_header header;
    uint32_t local_apic_address;
    uint32_t flags;
    uint8_t entries[]; // Variable length interrupt controller structures
} __attribute__((packed));

// MADT Entry Types
#define MADT_TYPE_LOCAL_APIC       0
#define MADT_TYPE_IO_APIC          1
#define MADT_TYPE_INTERRUPT_OVERRIDE 2
#define MADT_TYPE_NMI_SOURCE       3
#define MADT_TYPE_LOCAL_APIC_NMI   4
#define MADT_TYPE_LOCAL_APIC_ADDR_OVERRIDE 5

// MADT Entry Header
struct madt_entry_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

// Local APIC Entry
struct madt_local_apic {
    struct madt_entry_header header;
    uint8_t processor_id;
    uint8_t apic_id;
    uint32_t flags; // bit 0: Processor Enabled
} __attribute__((packed));

// I/O APIC Entry
struct madt_io_apic {
    struct madt_entry_header header;
    uint8_t io_apic_id;
    uint8_t reserved;
    uint32_t io_apic_address;
    uint32_t global_system_interrupt_base;
} __attribute__((packed));

// Interrupt Source Override Entry
struct madt_interrupt_override {
    struct madt_entry_header header;
    uint8_t bus;
    uint8_t source;
    uint32_t global_system_interrupt;
    uint16_t flags;
} __attribute__((packed));

// --------------------------------------------------------------------------
// ACPI Functions
// --------------------------------------------------------------------------

// Initialize ACPI subsystem
void acpi_init(void);

// Power management
void acpi_shutdown(void);
void acpi_reboot(void);

// Table access
struct acpi_sdt_header* acpi_find_table(const char *signature);
struct fadt* acpi_get_fadt(void);
struct madt* acpi_get_madt(void);

// APIC information
uint32_t acpi_get_local_apic_address(void);
int acpi_get_cpu_count(void);
void acpi_parse_madt(void);

// Enable ACPI mode
void acpi_enable(void);

#endif // KERNEL_ACPI_H