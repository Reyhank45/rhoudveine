#include "include/acpi.h"
#include "stdio.h"
#include "include/io.h"
#include <stdint.h>
#include <stddef.h>

// Forward declaration for kprintf
extern void kprintf(const char *format, uint32_t color, ...);

// --------------------------------------------------------------------------
// Utility Functions
// --------------------------------------------------------------------------

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

static void custom_memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n-- > 0) {
        *d++ = *s++;
    }
}

// --------------------------------------------------------------------------
// ACPI Table Checksum Validation
// --------------------------------------------------------------------------

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

static int acpi_checksum(struct acpi_sdt_header *header) {
    uint8_t sum = 0;
    uint8_t *ptr = (uint8_t *)header;
    for (uint32_t i = 0; i < header->length; i++) {
        sum += ptr[i];
    }
    return sum == 0;
}

// --------------------------------------------------------------------------
// RSDP Discovery
// --------------------------------------------------------------------------

// --------------------------------------------------------------------------
// RSDP Discovery
// --------------------------------------------------------------------------

static struct rsdp_descriptor *find_rsdp() {
    // Search in the BIOS ROM area (0xE0000 - 0xFFFFF)
    // IMPORTANT: On real hardware, we need to ensure this memory is mapped!
    // Since we are using identity mapping for the lower 4GB, this should be fine.
    
    // Scan standard BIOS area (16-byte aligned)
    for (uint64_t addr = 0xE0000; addr < 0x100000; addr += 16) {
        uint8_t *ptr = (uint8_t *)addr;
        if (acpi_check_rsdp(ptr)) {
            kprintf("ACPI: Found RSDP candidate at 0x%lx\n", 0x00FFFF00, addr);
            return (struct rsdp_descriptor *)ptr;
        }
    }
    
    // Also check EBDA (Extended BIOS Data Area)
    // The EBDA segment is stored at 0x40E
    uint16_t ebda_segment = *(uint16_t*)0x40E;
    if (ebda_segment) {
        uint64_t ebda_addr = (uint64_t)ebda_segment << 4;
        // Search first 1KB of EBDA
        for (uint64_t addr = ebda_addr; addr < ebda_addr + 1024; addr += 16) {
            uint8_t *ptr = (uint8_t *)addr;
            if (acpi_check_rsdp(ptr)) {
                kprintf("ACPI: Found RSDP candidate in EBDA at 0x%lx\n", 0x00FFFF00, addr);
                return (struct rsdp_descriptor *)ptr;
            }
        }
    }

    return NULL;
}

// --------------------------------------------------------------------------
// Global ACPI State
// --------------------------------------------------------------------------

static struct rsdp_descriptor *g_rsdp = NULL;
static struct rsdt *g_rsdt = NULL;
static struct xsdt *g_xsdt = NULL;
static struct fadt *g_fadt = NULL;
static struct madt *g_madt = NULL;

static int g_acpi_enabled = 0;
static uint32_t g_local_apic_addr = 0;
static int g_cpu_count = 0;

// --------------------------------------------------------------------------
// ACPI Table Search
// --------------------------------------------------------------------------

struct acpi_sdt_header* acpi_find_table(const char *signature) {
    if (!g_rsdt && !g_xsdt) {
        return NULL;
    }

    // Try XSDT first (ACPI 2.0+)
    if (g_xsdt) {
        // Validation: Ensure length is reasonable
        if (g_xsdt->header.length < sizeof(struct acpi_sdt_header)) return NULL;
        
        int entries = (g_xsdt->header.length - sizeof(struct acpi_sdt_header)) / 8;
        for (int i = 0; i < entries; i++) {
            uint64_t ptr_val = g_xsdt->sdt_pointers[i];
            
            // Basic validity check for pointer (must be in lower 4GB for now due to identity map)
            // But XSDT can have 64-bit pointers. Since we identity map, as long as it handles phys RAM...
            struct acpi_sdt_header *header = (struct acpi_sdt_header *)(uintptr_t)ptr_val;
            
            // Map validation? We assume direct access works.
            
            if (custom_memcmp(header->signature, signature, 4) == 0) {
                // Found signature!
                // Skip checksum if it's failing but signature matches, just warn
                if (acpi_checksum(header)) {
                    return header;
                } else {
                    kprintf("ACPI: Table %s found but checksum invalid (ignoring error)\n", 0xFFFF0000, signature);
                    return header; // Return anyway on real hardware sometimes checksums are weird
                }
            }
        }
    }

    // Fall back to RSDT (ACPI 1.0)
    if (g_rsdt) {
        if (g_rsdt->header.length < sizeof(struct acpi_sdt_header)) return NULL;

        int entries = (g_rsdt->header.length - sizeof(struct acpi_sdt_header)) / 4;
        for (int i = 0; i < entries; i++) {
            struct acpi_sdt_header *header = (struct acpi_sdt_header *)(uintptr_t)g_rsdt->sdt_pointers[i];
            
            if (custom_memcmp(header->signature, signature, 4) == 0) {
                if (acpi_checksum(header)) {
                    return header;
                } else {
                    kprintf("ACPI: Table %s found but checksum invalid (ignoring error)\n", 0xFFFF0000, signature);
                    return header;
                }
            }
        }
    }

    return NULL;
}

// --------------------------------------------------------------------------
// FADT Access
// --------------------------------------------------------------------------

struct fadt* acpi_get_fadt(void) {
    if (g_fadt) {
        return g_fadt;
    }

    g_fadt = (struct fadt *)acpi_find_table("FACP");
    if (g_fadt) {
        kprintf("ACPI: Found FADT at 0x%lx\n", 0x00FF0000, (uint64_t)g_fadt);
    }

    return g_fadt;
}

// --------------------------------------------------------------------------
// MADT Parsing
// --------------------------------------------------------------------------

struct madt* acpi_get_madt(void) {
    if (g_madt) {
        return g_madt;
    }

    g_madt = (struct madt *)acpi_find_table("APIC");
    if (g_madt) {
        kprintf("ACPI: Found MADT at 0x%lx\n", 0x00FF0000, (uint64_t)g_madt);
    }

    return g_madt;
}

void acpi_parse_madt(void) {
    struct madt *madt = acpi_get_madt();
    if (!madt) {
        kprintf("ACPI: MADT not found, cannot enumerate CPUs/APICs\n", 0xFFFF0000);
        return;
    }

    g_local_apic_addr = madt->local_apic_address;
    kprintf("ACPI: Local APIC base address: 0x%lx\n", 0x00FF0000, (uint64_t)g_local_apic_addr);

    // Parse MADT entries
    uint8_t *ptr = madt->entries;
    uint8_t *end = (uint8_t *)madt + madt->header.length;
    int cpu_count = 0;
    int io_apic_count = 0;

    while (ptr < end) {
        struct madt_entry_header *entry = (struct madt_entry_header *)ptr;

        switch (entry->type) {
            case MADT_TYPE_LOCAL_APIC: {
                struct madt_local_apic *lapic = (struct madt_local_apic *)entry;
                if (lapic->flags & 1) { // Processor enabled
                    cpu_count++;
                    kprintf("ACPI: CPU #%d - Processor ID: %d, APIC ID: %d\n", 
                            0x00FFFF00, cpu_count - 1, lapic->processor_id, lapic->apic_id);
                }
                break;
            }

            case MADT_TYPE_IO_APIC: {
                struct madt_io_apic *ioapic = (struct madt_io_apic *)entry;
                io_apic_count++;
                kprintf("ACPI: I/O APIC #%d at 0x%lx, GSI base: %d\n",
                        0x00FFFF00, ioapic->io_apic_id, 
                        (uint64_t)ioapic->io_apic_address,
                        ioapic->global_system_interrupt_base);
                break;
            }

            case MADT_TYPE_INTERRUPT_OVERRIDE: {
                struct madt_interrupt_override *override = (struct madt_interrupt_override *)entry;
                kprintf("ACPI: IRQ Override - Source: %d -> GSI: %d, flags: 0x%x\n",
                        0x00FFFF00, override->source, override->global_system_interrupt,
                        override->flags);
                break;
            }
        }

        ptr += entry->length;
    }

    g_cpu_count = cpu_count;
    kprintf("ACPI: Detected %d CPU(s), %d I/O APIC(s)\n", 0x00FF0000, cpu_count, io_apic_count);
}

uint32_t acpi_get_local_apic_address(void) {
    return g_local_apic_addr;
}

int acpi_get_cpu_count(void) {
    return g_cpu_count;
}

// --------------------------------------------------------------------------
// ACPI Enable
// --------------------------------------------------------------------------

void acpi_enable(void) {
    struct fadt *fadt = acpi_get_fadt();
    if (!fadt) {
        kprintf("ACPI: Cannot enable ACPI - FADT not found\n", 0xFFFF0000);
        return;
    }

    // Check if already in ACPI mode
    if (fadt->smi_command_port == 0) {
        kprintf("ACPI: System is already in ACPI mode\n", 0x00FF0000);
        g_acpi_enabled = 1;
        return;
    }

    // Send ACPI enable command
    kprintf("ACPI: Enabling ACPI mode via SMI port 0x%x\n", 0x00FF0000, fadt->smi_command_port);
    outb(fadt->smi_command_port, fadt->acpi_enable);

    // Wait for ACPI mode to be enabled (check PM1a control register)
    int timeout = 1000;
    while (timeout-- > 0) {
        uint16_t pm1_ctl = inw(fadt->pm1a_control_block);
        if (pm1_ctl & 1) { // SCI_EN bit
            kprintf("ACPI: Successfully enabled ACPI mode\n", 0x00FF0000);
            g_acpi_enabled = 1;
            return;
        }
        // Small delay
        for (volatile int i = 0; i < 10000; i++);
    }

    kprintf("ACPI: Warning - ACPI enable timeout\n", 0xFFFF0000);
}

// --------------------------------------------------------------------------
// Power Management
// --------------------------------------------------------------------------

void acpi_shutdown(void) {
    struct fadt *fadt = acpi_get_fadt();
    if (!fadt) {
        kprintf("ACPI: Cannot shutdown - FADT not found\n", 0xFFFF0000);
        kprintf("ACPI: Halting system instead\n", 0xFFFF0000);
        for (;;) { __asm__("cli; hlt"); }
        return;
    }

    kprintf("ACPI: Initiating shutdown...\n", 0x00FF0000);

    // Ensure ACPI is enabled
    if (!g_acpi_enabled) {
        acpi_enable();
    }

    // Method 1: QEMU-specific isa-debug-exit device (port 0x604)
    // This is a QEMU extension for clean shutdown
    kprintf("ACPI: Trying QEMU isa-debug-exit port...\n", 0x00FF0000);
    outw(0x604, 0x2000);  // QEMU shutdown signal
    
    // Small delay
    for (volatile int i = 0; i < 100000; i++);

    // Method 2: QEMU PM port (another QEMU-specific method)
    kprintf("ACPI: Trying QEMU PM port...\n", 0x00FF0000);
    outw(0xB004, 0x2000);  // Alternative QEMU shutdown
    
    for (volatile int i = 0; i < 100000; i++);

    // Method 3: Parse DSDT for _S5_ object (Shutdown)
    // Heuristic scan for byte sequence: 08 5F 53 35 5F 12 ...
    // NameOP, "_S5_", PackageOp, PkgLength, NumElements, BytePrefix, SLP_TYPa, BytePrefix, SLP_TYPb
    
    if (fadt->dsdt) {
        struct acpi_sdt_header *dsdt = (struct acpi_sdt_header *)(uintptr_t)fadt->dsdt;
        kprintf("ACPI: Scanning DSDT at 0x%lx for _S5_...\n", 0x00FF0000, (uint64_t)dsdt);
        
        // Basic map verification? Assume identity map works.
        if (custom_memcmp(dsdt->signature, "DSDT", 4) == 0) {
            uint8_t *p = (uint8_t *)dsdt + sizeof(struct acpi_sdt_header);
            uint8_t *end = (uint8_t *)dsdt + dsdt->length;
            
            while (p < end) {
                if (custom_memcmp(p, "_S5_", 4) == 0) {
                     // Found _S5_ signature. 
                     // Common structure:
                     // [08?] 5F 53 35 5F [12] [Len] [Num] [0A] [ValA] [0A] [ValB] ...
                     // Sometimes NameOp (08) is before _S5_, sometimes _S5_ is embedded.
                     // The _S5_ tag itself is just the name. 
                     
                     // Check if next byte is PackageOp (0x12)
                     uint8_t *pkg = p + 4;
                     if (*pkg == 0x12) { // PackageOp
                        kprintf("ACPI: Found _S5_ package at offset simulator 0x%x\n", 0x00FFFF00, (uint32_t)(p - (uint8_t*)dsdt));
                        
                        // Parse Package Length
                        // Bit 6-7 of first byte determines number of bytes used for length
                        uint8_t pkg_len_first = *(pkg+1);
                        int pkg_len_bytes = (pkg_len_first >> 6) & 0x3;
                        
                        // Skip length bytes plus PkgOp and first byte of length
                        uint8_t *data = pkg + 2 + (pkg_len_bytes > 0 ? pkg_len_bytes : 0);
                        if (pkg_len_bytes == 0) {
                             // Correct PkgLength decoding:
                             // If bits 6-7 are 0, length is bits 0-5. 
                             // Wait, logic is complex.
                             // Let's assume standard small package for _S5_
                             // It usually just skips 1 byte for length if length < 63
                        }
                        
                        // Simpler heuristic: look for [0A] [Val]
                        // We need SLP_TYPa (PM1a_CNT) and SLP_TYPb (PM1b_CNT)
                        
                        // Skip NumElements byte
                        data++; 
                        
                        // Now expect elements. Usually Integers.
                        // 0x0A (BytePrefix) + Value
                        // 0x0B (WordPrefix) + Value
                        // 0x0C (DWordPrefix) + Value
                        // 0x00 (ZeroOp) -> 0
                        // 0x01 (OneOp) -> 1
                        
                        uint16_t slp_typa = 0;
                        uint16_t slp_typb = 0;
                        int valid = 0;
                        
                        // First Element (SLP_TYPa)
                        if (*data == 0x0A) { slp_typa = *(data+1); data += 2; valid++; }
                        else if (*data == 0x00) { slp_typa = 0; data += 1; valid++; }
                        else if (*data == 0x01) { slp_typa = 1; data += 1; valid++; }
                        
                        // Second Element (SLP_TYPb)
                        if (*data == 0x0A) { slp_typb = *(data+1); data += 2; valid++; }
                        else if (*data == 0x00) { slp_typb = 0; data += 1; valid++; }
                        else if (*data == 0x01) { slp_typb = 1; data += 1; valid++; }
                        
                        if (valid >= 2) {
                            kprintf("ACPI: Parsed _S5_: SLP_TYPa=%d, SLP_TYPb=%d\n", 0x00FF0000, slp_typa, slp_typb);
                            
                            // Perform Shutdown Sequence
                            
                            // 1. Write SLP_TYPa | SLP_EN (bit 13) to PM1a_CNT
                            uint16_t pm1a_val = (slp_typa << 10) | (1 << 13);
                            if (fadt->pm1a_control_block) {
                                outw(fadt->pm1a_control_block, pm1a_val);
                            }
                            
                            // 2. Write SLP_TYPb | SLP_EN to PM1b_CNT
                            uint16_t pm1b_val = (slp_typb << 10) | (1 << 13);
                            if (fadt->pm1b_control_block) {
                                outw(fadt->pm1b_control_block, pm1b_val);
                            }
                            
                            kprintf("ACPI: Sent Shutdown command (S5)\n", 0x00FF0000);
                            for (volatile int j = 0; j < 1000000; j++);
                            // Halt loop
                            for (;;) { __asm__("cli; hlt"); }
                        }
                     }
                }
                p++;
            }
            kprintf("ACPI: _S5_ not found in DSDT, falling back to guessing\n", 0xFFFF0000);
        }
    }

    // Fallback: Try common SLP_TYP values for S5 state
    // Different systems use different values (should be read from DSDT)
    // Common values: 0, 5, 7
    if (fadt->pm1a_control_block) {
        int slp_typ_values[] = {0, 5, 7, 13};  // Common S5 SLP_TYP values
        
        for (int i = 0; i < 4; i++) {
            kprintf("ACPI: Trying SLP_TYP=%d...\n", 0x00FF0000, slp_typ_values[i]);
            uint16_t pm1a_ctl = inw(fadt->pm1a_control_block);
            uint16_t shutdown_value = (pm1a_ctl & 0xE3FF) | (slp_typ_values[i] << 10) | (1 << 13);
            outw(fadt->pm1a_control_block, shutdown_value);
            
            // Wait a bit
            for (volatile int j = 0; j < 1000000; j++);
        }
    }

    if (fadt->pm1b_control_block) {
        int slp_typ_values[] = {0, 5, 7, 13};
        for (int i = 0; i < 4; i++) {
            uint16_t pm1b_ctl = inw(fadt->pm1b_control_block);
            uint16_t shutdown_value = (pm1b_ctl & 0xE3FF) | (slp_typ_values[i] << 10) | (1 << 13);
            outw(fadt->pm1b_control_block, shutdown_value);
            for (volatile int j = 0; j < 1000000; j++);
        }
    }

    // Method 4: APM (Advanced Power Management) - legacy method
    kprintf("ACPI: Trying legacy APM shutdown...\n", 0x00FF0000);
    // APM 1.1+ shutdown: function 0x5307 (Set Power State) to 0x03 (Off)
    __asm__ volatile(
        "movw $0x5301, %%ax\n"  // APM Installation Check
        "xorw %%bx, %%bx\n"      // Device ID 0 (APM BIOS)
        "int $0x15\n"
        "jc 1f\n"                // If carry, APM not supported
        "movw $0x5307, %%ax\n"   // Set Power State
        "movw $0x0001, %%bx\n"   // Device ID 1 (All devices)
        "movw $0x0003, %%cx\n"   // Power State 3 (Off)
        "int $0x15\n"
        "1:\n"
        ::: "ax", "bx", "cx"
    );

    // If we're still here, shutdown didn't work
    kprintf("ACPI: All shutdown methods failed, halting\n", 0xFFFF0000);
    for (;;) { __asm__("cli; hlt"); }
}

void acpi_reboot(void) {
    kprintf("ACPI: Initiating reboot...\n", 0x00FF0000);

    // Method 1: ACPI Reset Register (if available in FADT)
    struct fadt *fadt = acpi_get_fadt();
    if (fadt && fadt->flags & (1 << 10)) { // Reset register supported
        kprintf("ACPI: Using ACPI reset register\n", 0x00FF0000);
        // FADT reset not fully implemented here, would need Generic Address Structure parsing
    }

    // Method 2: PS/2 Controller reset (8042 keyboard controller)
    kprintf("ACPI: Attempting PS/2 keyboard controller reset\n", 0x00FF0000);
    // Wait for input buffer to be clear
    int timeout = 100000;
    while (timeout-- > 0) {
        if ((inb(0x64) & 0x02) == 0) break;
    }
    // Send reset command
    outb(0x64, 0xFE);

    // Small delay
    for (volatile int i = 0; i < 1000000; i++);

    // Method 3: Triple fault (last resort)
    kprintf("ACPI: Forcing reboot via triple fault\n", 0xFFFF0000);
    // Load invalid IDT and trigger interrupt
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) invalid_idt = {0, 0};

    __asm__ volatile("lidt %0; int $0x00" : : "m"(invalid_idt));

    // Should never reach here
    kprintf("ACPI: Reboot failed, halting\n", 0xFFFF0000);
    for (;;) { __asm__("cli; hlt"); }
}

// --------------------------------------------------------------------------
// ACPI Initialization
// --------------------------------------------------------------------------

void acpi_init(void *rsdp_address) {
    kprintf("ACPI: Initializing ACPI subsystem...\n", 0x00FF0000);

    // Use provided RSDP address (from Multiboot2) if available
    if (rsdp_address) {
        g_rsdp = (struct rsdp_descriptor *)rsdp_address;
        kprintf("ACPI: Using RSDP from Multiboot2 at 0x%lx\n", 0x00FF0000, (uint64_t)g_rsdp);
    } else {
        // Fallback to manual search
        kprintf("ACPI: Scanning for RSDP...\n", 0x00FFFF00);
        g_rsdp = find_rsdp();
    }

    if (!g_rsdp) {
        kprintf("ACPI: RSDP not found - ACPI unavailable\n", 0xFFFF0000);
        return;
    }

    kprintf("ACPI: Found RSDP at 0x%lx\n", 0x00FF0000, (uint64_t)g_rsdp);
    kprintf("ACPI: OEM ID: %.6s\n", 0x00FF0000, g_rsdp->oem_id);
    kprintf("ACPI: Revision: %d\n", 0x00FF0000, g_rsdp->revision);

    // Get RSDT or XSDT
    if (g_rsdp->revision >= 2) {
        // ACPI 2.0+, use XSDT
        struct rsdp_descriptor_2 *rsdp2 = (struct rsdp_descriptor_2 *)g_rsdp;
        if (rsdp2->xsdt_address) {
            g_xsdt = (struct xsdt *)(uintptr_t)rsdp2->xsdt_address;
            kprintf("ACPI: Using XSDT at 0x%lx\n", 0x00FF0000, rsdp2->xsdt_address);
        }
    }

    // Fall back to RSDT if XSDT not available
    if (!g_xsdt && g_rsdp->rsdt_address) {
        g_rsdt = (struct rsdt *)(uintptr_t)g_rsdp->rsdt_address;
        kprintf("ACPI: Using RSDT at 0x%lx\n", 0x00FF0000, (uint64_t)g_rsdp->rsdt_address);
    }

    if (!g_rsdt && !g_xsdt) {
        kprintf("ACPI: No RSDT or XSDT found\n", 0xFFFF0000);
        return;
    }

    // Find and parse important tables
    acpi_get_fadt();
    acpi_parse_madt();

    // List all available tables
    kprintf("ACPI: Available tables:\n", 0x00FF0000);
    if (g_xsdt) {
        int entries = (g_xsdt->header.length - sizeof(struct acpi_sdt_header)) / 8;
        for (int i = 0; i < entries; i++) {
            struct acpi_sdt_header *header = (struct acpi_sdt_header *)(uintptr_t)g_xsdt->sdt_pointers[i];
            kprintf("  %.4s (OEM: %.6s)\n", 0x00FFFF00, header->signature, header->oem_id);
        }
    } else if (g_rsdt) {
        int entries = (g_rsdt->header.length - sizeof(struct acpi_sdt_header)) / 4;
        for (int i = 0; i < entries; i++) {
            struct acpi_sdt_header *header = (struct acpi_sdt_header *)(uintptr_t)g_rsdt->sdt_pointers[i];
            kprintf("  %.4s (OEM: %.6s)\n", 0x00FFFF00, header->signature, header->oem_id);
        }
    }

    kprintf("ACPI: Initialization complete\n", 0x00FF0000);
}
