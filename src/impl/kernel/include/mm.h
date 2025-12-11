#ifndef KERNEL_MM_H
#define KERNEL_MM_H

#include <stdint.h>
#include <stddef.h>

// Initialize the physical memory manager.
void mm_init(uint64_t multiboot_addr);

// Maps a region of physical memory into the kernel's MMIO virtual address space.
// Returns the virtual address.
void *mmio_remap(uint64_t physical_addr, size_t size);

#endif // KERNEL_MM_H