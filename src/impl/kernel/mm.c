#include <stdint.h>
#include "include/mm.h"
#include "stdio.h"
#include <stddef.h>

// Forward declaration for kprintf
extern void kprintf(const char *format, uint32_t color, ...);

// Symbol from the linker script, marks the end of the kernel image.
extern uint8_t kernel_end[];

// Multiboot tag structures needed for memory map
#define PACKED __attribute__((packed))
struct multiboot_tag { uint32_t type; uint32_t size; } PACKED;
struct multiboot_tag_mmap {
    struct multiboot_tag common;
    uint32_t entry_size;
    uint32_t entry_version;
    struct multiboot_mmap_entry {
        uint64_t addr;
        uint64_t len;
        uint32_t type;
        uint32_t reserved;
    } PACKED entries[];
} PACKED;

#define MULTIBOOT_MEMORY_AVAILABLE 1
#define PAGE_SIZE 4096

// The virtual address offset where all of physical memory is mapped.
// This is a fundamental architectural decision. The bootloader must set up
// page tables that map all physical memory to this offset before jumping to the kernel.
// For example, physical address 0x1000 is accessible at virtual 0xFFFF800000001000.
#define DIRECT_MAP_OFFSET 0xFFFF800000000000
static inline void* phys_to_virt(uint64_t paddr) { return (void*)(paddr + DIRECT_MAP_OFFSET); }

// Custom memset
static void *custom_memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n-- > 0) *p++ = (unsigned char)c;
    return s;
}

// --- Physical Frame Allocator (PFA) ---
// A simple stack-based allocator. Assumes we have enough space in .bss
// to hold the stack for all available memory.
#define MAX_PAGES (1024*1024*2) // Support up to 8GB of RAM
static uint64_t page_stack[MAX_PAGES];
static int64_t page_stack_top = -1;

void pfa_free(uint64_t paddr) {
    if (page_stack_top < MAX_PAGES - 1) {
        page_stack[++page_stack_top] = paddr;
    }
}


uint64_t pfa_alloc() {
    if (page_stack_top >= 0) {
        return page_stack[page_stack_top--];
    }
    // This is a fatal error, we're out of memory.
    // A real kernel would panic. For now, return 0.
    kprintf("MM: FATAL - pfa_alloc failed (out of memory)!\n", 0xFF0000);
    return 0;
}

// --- Virtual Memory Manager (VMM) ---
#define PAGE_PRESENT (1 << 0)
#define PAGE_RW      (1 << 1)
#define PAGE_USER    (1 << 2)
#define PAGE_PWT     (1 << 3)
#define PAGE_PCD     (1 << 4) // Page Cache Disable
#define PAGE_NO_EXEC (1ULL << 63)

// MMIO virtual address region. Initialized in mm_init.
static uint64_t next_mmio_addr;

// This function maps a single page.
static void map_page(uint64_t phys_addr, uint64_t virt_addr, uint64_t flags) {
    uint64_t pml4_index = (virt_addr >> 39) & 0x1FF;
    uint64_t pdpt_index = (virt_addr >> 30) & 0x1FF;
    uint64_t pdt_index  = (virt_addr >> 21) & 0x1FF;
    uint64_t pt_index   = (virt_addr >> 12) & 0x1FF;

    // Extract caching flags to apply them to all levels of the page table hierarchy for consistency.
    uint64_t caching_flags = flags & (PAGE_PWT | PAGE_PCD);

    // Define a mask to extract the physical address from a page table entry.
    // This clears flags (lower 12 bits) and any implementation-defined bits (upper 12 bits).
    // It preserves the 40-bit physical address field (bits 12 to 51).
    const uint64_t PADDR_MASK = 0x000FFFFFFFFFF000;

    uint64_t pml4_phys;
    asm volatile("mov %%cr3, %0" : "=r"(pml4_phys));
    
    // Access all page table levels through the direct physical map.
    // This assumes the bootloader has created this mapping for the kernel to use.
    uint64_t *pml4_virt = phys_to_virt(pml4_phys & PADDR_MASK);

    // PML4 -> PDPT
    if (!(pml4_virt[pml4_index] & PAGE_PRESENT)) {
        uint64_t pdpt_phys = pfa_alloc();
        if (!pdpt_phys) {
            kprintf("MM: map_page failed to allocate PDPT for virt 0x%lx\n", 0xFF0000, virt_addr);
            return; // Out of memory
        }
        custom_memset(phys_to_virt(pdpt_phys), 0, PAGE_SIZE);
        pml4_virt[pml4_index] = pdpt_phys | PAGE_PRESENT | PAGE_RW | caching_flags;
    } else {
        // Entry already exists, ensure caching flags are set for the hierarchy.
        pml4_virt[pml4_index] |= caching_flags;
    }
    uint64_t *pdpt_virt = phys_to_virt(pml4_virt[pml4_index] & PADDR_MASK);

    // PDPT -> PDT
    if (!(pdpt_virt[pdpt_index] & PAGE_PRESENT)) {
        uint64_t pdt_phys = pfa_alloc();
        if (!pdt_phys) {
            kprintf("MM: map_page failed to allocate PDT for virt 0x%lx\n", 0xFF0000, virt_addr);
            return;
        }
        custom_memset(phys_to_virt(pdt_phys), 0, PAGE_SIZE);
        pdpt_virt[pdpt_index] = pdt_phys | PAGE_PRESENT | PAGE_RW | caching_flags;
    } else {
        pdpt_virt[pdpt_index] |= caching_flags;
    }
    uint64_t *pdt_virt = phys_to_virt(pdpt_virt[pdpt_index] & PADDR_MASK);

    // PDT -> PT
    if (!(pdt_virt[pdt_index] & PAGE_PRESENT)) {
        uint64_t pt_phys = pfa_alloc();
        if (!pt_phys) {
            kprintf("MM: map_page failed to allocate PT for virt 0x%lx\n", 0xFF0000, virt_addr);
            return;
        }
        custom_memset(phys_to_virt(pt_phys), 0, PAGE_SIZE);
        pdt_virt[pdt_index] = pt_phys | PAGE_PRESENT | PAGE_RW | caching_flags;
    } else {
        pdt_virt[pdt_index] |= caching_flags;
    }
    uint64_t *pt_virt = phys_to_virt(pdt_virt[pdt_index] & PADDR_MASK);

    // Map the page in the Page Table
    pt_virt[pt_index] = phys_addr | flags;

    // Ensure all writes to page tables are globally visible before flushing TLB
    asm volatile("mfence" ::: "memory");

    // A full CR3 reload is necessary to flush all paging-structure caches (for the
    // hierarchy) and the TLB. This is simpler and more robust than invlpg.
    asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
}

void mm_init(uint64_t multiboot_addr) {
    kprintf("MM: Initializing memory manager...\n", 0x00FF0000);
    struct multiboot_tag *tag = (struct multiboot_tag *)(multiboot_addr + 8);
    struct multiboot_tag_mmap *mmap_tag = NULL;

    // Find the memory map tag
    for (; tag->type != 0; tag = (struct multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7))) {
        if (tag->type == 6) { // Memory map tag
            mmap_tag = (struct multiboot_tag_mmap *)tag;
            break;
        }
    }

    if (!mmap_tag) {
        kprintf("MM: FATAL - Multiboot memory map not found!\n", 0xFF0000);
        return;
    }

    kprintf("MM: mmap_tag->common.size = %u, mmap_tag->entry_size = %u\n", 0x00FF0000, mmap_tag->common.size, mmap_tag->entry_size);

    // Get the physical address of the end of the kernel from the linker script.
    uint64_t kernel_end_addr = (uint64_t)&kernel_end;
    // Align the kernel end address up to the next page boundary to be safe.
    if (kernel_end_addr % PAGE_SIZE != 0) {
        kernel_end_addr = (kernel_end_addr + PAGE_SIZE) & ~(PAGE_SIZE - 1);
    }
    kprintf("MM: Kernel image ends at 0x%lx. Reserving memory below this.\n", 0x00FF0000, kernel_end_addr);

    // Initialize physical frame allocator
    uint32_t num_entries = (mmap_tag->common.size - sizeof(*mmap_tag)) / mmap_tag->entry_size;
    kprintf("MM: Detected %u memory map entries.\n", 0x00FF0000, num_entries);
    for (uint32_t i = 0; i < num_entries; i++) {
        struct multiboot_mmap_entry *entry = &mmap_tag->entries[i];
        if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
            kprintf("MM: Usable RAM at 0x%lx, size 0x%lx\n", 0x00FF0000, entry->addr, entry->len);            
            // Align start up and end down to page boundaries.
            uint64_t first_page = (entry->addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            uint64_t last_page = (entry->addr + entry->len) & ~(PAGE_SIZE - 1);
            
            // Add pages in reverse order so the allocator starts with lower addresses.
            for (uint64_t p = last_page; p > first_page; ) {
                p -= PAGE_SIZE; 
                // Don't use pages occupied by the kernel image itself.
                if (p < kernel_end_addr) {
                    continue;
                }
                pfa_free(p);
            }
        }
    }
    kprintf("MM: PFA initialized with %ld free pages.\n", 0x00FF0000, page_stack_top + 1);

    // Initialize next_mmio_addr to be just after the kernel image.
    // We will place the MMIO region at a high, fixed virtual address
    // to avoid any potential collision with the kernel or future allocations.
    next_mmio_addr = 0xFFFFFFFF80000000; // Start MMIO region in the higher half
    kprintf("MM: MMIO mapping region starts at 0x%lx\n", 0x00FF0000, next_mmio_addr);
}

void *mmio_remap(uint64_t physical_addr, size_t size) {
    uint64_t virt_start = next_mmio_addr;
    uint64_t phys_start_aligned = physical_addr & ~(PAGE_SIZE - 1);
    uint64_t virt_start_aligned = virt_start & ~(PAGE_SIZE - 1);
    
    size_t offset = physical_addr - phys_start_aligned;
    size_t num_pages = (offset + size - 1) / PAGE_SIZE + 1;

    kprintf("MM: Remapping phys 0x%lx -> virt 0x%lx (pages: %lu)\n", 0x00FF0000, physical_addr, virt_start, (unsigned long)num_pages);

    for (size_t i = 0; i < num_pages; i++) {
        uint64_t p_addr = phys_start_aligned + i * PAGE_SIZE;
        uint64_t v_addr = virt_start_aligned + i * PAGE_SIZE;
        map_page(p_addr, v_addr, PAGE_PRESENT | PAGE_RW | PAGE_PCD | PAGE_PWT | PAGE_NO_EXEC);
    }

    next_mmio_addr = virt_start_aligned + num_pages * PAGE_SIZE;
    return (void *)(virt_start + (physical_addr - phys_start_aligned));
}

uint64_t virt_to_phys(void* vaddr) {
    uint64_t addr = (uint64_t)vaddr;

    // If the address is in the higher-half direct map region, convert it.
    if (addr >= DIRECT_MAP_OFFSET) {
        return addr - DIRECT_MAP_OFFSET;
    }

    // Otherwise, assume it's a low-memory identity-mapped address for the kernel's
    // own code and data. In this memory model, the virtual address is the physical address.
    // This is a simplification; a more complex kernel might have a different offset.
    // We return the address as-is, assuming virt == phys for the kernel image.
    return addr;
}