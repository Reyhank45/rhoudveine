#ifndef KERNEL_MM_H
#define KERNEL_MM_H

#include <stdint.h>
#include <stddef.h>

// Initialize the physical memory manager.
void mm_init(uint64_t multiboot_addr);

// Maps a region of physical memory into the kernel's MMIO virtual address space.
// Returns the virtual address.
void *mmio_remap(uint64_t physical_addr, size_t size);

// Allocate a physical frame (4KB page). Returns physical address.
uint64_t pfa_alloc(void);

// Free a physical frame
void pfa_free(uint64_t paddr);

// Convert physical address to virtual address
void *phys_to_virt(uint64_t paddr);

// Convert virtual address to physical address
uint64_t virt_to_phys(void *vaddr);

#endif // KERNEL_MM_H