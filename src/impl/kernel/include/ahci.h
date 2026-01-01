#ifndef KERNEL_AHCI_H
#define KERNEL_AHCI_H

#include <stdint.h>

// AHCI initialization
int ahci_init(void);

// Read sectors from AHCI disk
// lba: Logical Block Address
// count: Number of 512-byte sectors to read
// buffer: Destination buffer (must be large enough)
// Returns 0 on success, -1 on error
int ahci_read_sectors(uint64_t lba, uint32_t count, uint8_t *buffer);

// Write sectors to AHCI disk
int ahci_write_sectors(uint64_t lba, uint32_t count, const uint8_t *buffer);

// Get number of AHCI ports found
int ahci_get_port_count(void);

// Check if AHCI is initialized
int ahci_is_initialized(void);

#endif // KERNEL_AHCI_H
