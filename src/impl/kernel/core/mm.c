#include "include/mm.h"
#include "include/stdio.h"
#include <stddef.h>
#include <stdint.h>

// Forward declaration for kprintf
extern void kprintf(const char *format, uint32_t color, ...);

// Symbol from the linker script, marks the end of the kernel image.
extern uint8_t kernel_end[];

// Multiboot tag structures needed for memory map
#define PACKED __attribute__((packed))
struct multiboot_tag {
  uint32_t type;
  uint32_t size;
} PACKED;
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
// page tables that map all physical memory to this offset before jumping to the
// kernel. For example, physical address 0x1000 is accessible at virtual
// 0xFFFF800000001000.
#define DIRECT_MAP_OFFSET 0xFFFF800000000000

// TEMPORARY FIX for real hardware: Use identity mapping
// The bootloader only sets up identity mapping (virt == phys) for first 4GB.
// Higher-half mapping will be added in Phase 2.
// TODO: Set up proper higher-half mapping in boot/main.asm
void *phys_to_virt(uint64_t paddr) {
  return (void *)paddr; // Identity mapping for now
}

// Custom memset
static void *custom_memset(void *s, int c, size_t n) {
  unsigned char *p = s;
  while (n-- > 0)
    *p++ = (unsigned char)c;
  return s;
}

// ---------------------------------------------------------------------------
// Physical Frame Allocator (PFA) — Bitmap Implementation
//
// Uses one bit per 4KB page:
//   0 = free
//   1 = used
//
// Two separate bitmaps:
//   low_bitmap  — tracks pages below 4GB (used for page table allocations
//                 which must be identity-mapped and accessible via CR3)
//   bitmap      — tracks pages at or above 4GB (general kernel allocations)
//
// Memory cost vs old stack allocator:
//   Stack:  MAX_PAGES x 8 bytes = 16MB in .bss
//   Bitmap: MAX_PAGES / 8 bytes = 256KB in .bss  (64x smaller)
// ---------------------------------------------------------------------------

#define MAX_PAGES (1024ULL * 1024 * 2) // Support up to 8GB of RAM
#define BITMAP_SIZE (MAX_PAGES / 8)    // 256KB per bitmap

// Main bitmap — high memory (>=4GB)
static uint8_t bitmap[BITMAP_SIZE];
static uint64_t bitmap_base = 0; // lowest physical addr tracked by main bitmap
static uint64_t free_pages_count = 0;  // current free pages in main bitmap
static uint64_t total_pages_count = 0; // total pages registered in main bitmap

// Low bitmap — low memory (<4GB, identity mapped)
static uint8_t low_bitmap[BITMAP_SIZE];
static uint64_t low_free_count = 0;  // current free pages in low bitmap
static uint64_t low_total_count = 0; // total pages registered in low bitmap

// ---------------------------------------------------------------------------
// Bitmap helper functions
// ---------------------------------------------------------------------------

// Convert a high physical address to a main bitmap index.
static inline uint64_t addr_to_index(uint64_t paddr) {
  return (paddr - bitmap_base) / PAGE_SIZE;
}

// Mark a page as used (set bit to 1).
static inline void bitmap_set_used(uint8_t *bmap, uint64_t index) {
  bmap[index / 8] |= (uint8_t)(1 << (index % 8));
}

// Mark a page as free (clear bit to 0).
static inline void bitmap_set_free(uint8_t *bmap, uint64_t index) {
  bmap[index / 8] &= (uint8_t)~(1 << (index % 8));
}

// Returns 1 if page is free (bit == 0), 0 if used (bit == 1).
static inline int bitmap_is_free(uint8_t *bmap, uint64_t index) {
  return !(bmap[index / 8] & (uint8_t)(1 << (index % 8)));
}

// ---------------------------------------------------------------------------
// PFA public API
// ---------------------------------------------------------------------------

// Free (return) a physical page back to the allocator.
// Pages below 4GB go into the low bitmap.
// Pages at or above 4GB go into the main bitmap.
void pfa_free(uint64_t paddr) {
  if (paddr == 0)
    return; // never free the null page

  if (paddr < 0x100000000ULL) {
    // Low memory path (<4GB)
    uint64_t index = paddr / PAGE_SIZE;
    if (index >= MAX_PAGES)
      return;

    // Only count it if it was previously marked used
    if (!bitmap_is_free(low_bitmap, index)) {
      bitmap_set_free(low_bitmap, index);
      low_free_count++;
      low_total_count++;
    }
    return;
  }

  // High memory path (>=4GB)
  if (paddr < bitmap_base)
    return;
  uint64_t index = addr_to_index(paddr);
  if (index >= MAX_PAGES)
    return;

  if (!bitmap_is_free(bitmap, index)) {
    bitmap_set_free(bitmap, index);
    free_pages_count++;
    total_pages_count++;
  }
}

// Allocate one page from low memory (<4GB).
// Required for page table allocations since those physical addresses
// must be reachable through the identity mapping set up by the bootloader.
uint64_t pfa_alloc_low(void) {
  // Fast scan: skip fully-used bytes (0xFF = all 8 pages used)
  for (uint64_t byte = 0; byte < BITMAP_SIZE; byte++) {
    if (low_bitmap[byte] == 0xFF)
      continue; // all 8 pages in this byte used

    // At least one free page in this byte — find which bit
    for (uint64_t bit = 0; bit < 8; bit++) {
      uint64_t index = byte * 8 + bit;
      if (index >= MAX_PAGES)
        break;

      if (bitmap_is_free(low_bitmap, index)) {
        bitmap_set_used(low_bitmap, index);
        low_free_count--;
        return index * PAGE_SIZE; // low bitmap index IS the page number
      }
    }
  }

  kprintf("MM: pfa_alloc_low() out of low memory!\n", 0xFF0000);
  return 0;
}

// Allocate one page from general (high) memory.
uint64_t pfa_alloc(void) {
  // Fast scan: skip fully-used bytes
  for (uint64_t byte = 0; byte < BITMAP_SIZE; byte++) {
    if (bitmap[byte] == 0xFF)
      continue;

    for (uint64_t bit = 0; bit < 8; bit++) {
      uint64_t index = byte * 8 + bit;
      if (index >= MAX_PAGES)
        break;

      if (bitmap_is_free(bitmap, index)) {
        bitmap_set_used(bitmap, index);
        free_pages_count--;
        return bitmap_base + (index * PAGE_SIZE);
      }
    }
  }

  kprintf("MM: pfa_alloc() out of memory!\n", 0xFF0000);
  return 0;
}

// ---------------------------------------------------------------------------
// Memory statistics
// ---------------------------------------------------------------------------

uint64_t mm_get_total_memory(void) {
  return (total_pages_count + low_total_count) * PAGE_SIZE;
}

uint64_t mm_get_free_memory(void) {
  return (free_pages_count + low_free_count) * PAGE_SIZE;
}

// ---------------------------------------------------------------------------
// Virtual Memory Manager (VMM)
// ---------------------------------------------------------------------------

#define PAGE_PRESENT (1 << 0)
#define PAGE_RW (1 << 1)
#define PAGE_USER (1 << 2)
#define PAGE_PWT (1 << 3)
#define PAGE_PCD (1 << 4) // Page Cache Disable
#define PAGE_NO_EXEC (1ULL << 63)

// MMIO virtual address region. Initialized in mm_init.
static uint64_t next_mmio_addr;

// Maps a single 4KB page: phys_addr -> virt_addr with given flags.
// Walks/creates the 4-level page table hierarchy (PML4 -> PDPT -> PD -> PT).
// All intermediate page table pages are allocated from low memory so they
// are reachable through the bootloader's identity mapping.
static void map_page(uint64_t phys_addr, uint64_t virt_addr, uint64_t flags) {
  uint64_t pml4_index = (virt_addr >> 39) & 0x1FF;
  uint64_t pdpt_index = (virt_addr >> 30) & 0x1FF;
  uint64_t pdt_index = (virt_addr >> 21) & 0x1FF;
  uint64_t pt_index = (virt_addr >> 12) & 0x1FF;

  // Extract caching flags to propagate through all hierarchy levels.
  uint64_t caching_flags = flags & (PAGE_PWT | PAGE_PCD);

  // Mask to extract the physical address from a PTE (bits 12-51).
  // Clears the flag bits (lower 12) and reserved/NX bits (upper 12).
  const uint64_t PADDR_MASK = 0x000FFFFFFFFFF000ULL;

  uint64_t pml4_phys;
  asm volatile("mov %%cr3, %0" : "=r"(pml4_phys));

  // Access all page table levels through the direct physical map.
  uint64_t *pml4_virt = phys_to_virt(pml4_phys & PADDR_MASK);

  // PML4 -> PDPT
  if (!(pml4_virt[pml4_index] & PAGE_PRESENT)) {
    uint64_t pdpt_phys = pfa_alloc_low();
    if (!pdpt_phys) {
      kprintf("MM: map_page failed to allocate PDPT for virt 0x%lx\n", 0xFF0000,
              virt_addr);
      return;
    }
    custom_memset(phys_to_virt(pdpt_phys), 0, PAGE_SIZE);
    pml4_virt[pml4_index] = pdpt_phys | PAGE_PRESENT | PAGE_RW | caching_flags;
  } else {
    pml4_virt[pml4_index] |= caching_flags;
  }
  uint64_t *pdpt_virt = phys_to_virt(pml4_virt[pml4_index] & PADDR_MASK);

  // PDPT -> PDT
  if (!(pdpt_virt[pdpt_index] & PAGE_PRESENT)) {
    uint64_t pdt_phys = pfa_alloc_low();
    if (!pdt_phys) {
      kprintf("MM: map_page failed to allocate PDT for virt 0x%lx\n", 0xFF0000,
              virt_addr);
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
    uint64_t pt_phys = pfa_alloc_low();
    if (!pt_phys) {
      kprintf("MM: map_page failed to allocate PT for virt 0x%lx\n", 0xFF0000,
              virt_addr);
      return;
    }
    custom_memset(phys_to_virt(pt_phys), 0, PAGE_SIZE);
    pdt_virt[pdt_index] = pt_phys | PAGE_PRESENT | PAGE_RW | caching_flags;
  } else {
    pdt_virt[pdt_index] |= caching_flags;
  }
  uint64_t *pt_virt = phys_to_virt(pdt_virt[pdt_index] & PADDR_MASK);

  // Finally map the page in the Page Table.
  pt_virt[pt_index] = phys_addr | flags;

  // Ensure all page table writes are globally visible before flushing the TLB.
  asm volatile("mfence" ::: "memory");

  // Full CR3 reload flushes all paging-structure caches and the TLB.
  // More robust than invlpg when mapping new hierarchy levels.
  asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
}

// ---------------------------------------------------------------------------
// mm_init — entry point, called once at kernel startup
// ---------------------------------------------------------------------------

void mm_init(uint64_t multiboot_addr) {
  kprintf("MM: Initializing memory manager...\n", 0x00FF0000);

  // -----------------------------------------------------------------------
  // Step 1 — Initialize both bitmaps to all-used (0xFF).
  // We start with every page marked as used and only free pages that the
  // multiboot memory map tells us are actually available RAM.
  // This is safer than starting free: any gap in the map stays reserved.
  // -----------------------------------------------------------------------
  custom_memset(bitmap, 0xFF, BITMAP_SIZE);
  custom_memset(low_bitmap, 0xFF, BITMAP_SIZE);

  // -----------------------------------------------------------------------
  // Step 2 — Parse multiboot tags to find the memory map.
  // -----------------------------------------------------------------------
  struct multiboot_tag *tag = (struct multiboot_tag *)(multiboot_addr + 8);
  struct multiboot_tag_mmap *mmap_tag = NULL;

  for (; tag->type != 0;
       tag =
           (struct multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7))) {
    if (tag->type == 6) {
      mmap_tag = (struct multiboot_tag_mmap *)tag;
      break;
    }
  }

  if (!mmap_tag) {
    kprintf("MM: FATAL - Multiboot memory map not found!\n", 0xFF0000);
    return;
  }

  kprintf("MM: mmap_tag->common.size = %u, mmap_tag->entry_size = %u\n",
          0x00FF0000, mmap_tag->common.size, mmap_tag->entry_size);

  // -----------------------------------------------------------------------
  // Step 3 — Determine kernel image boundary.
  // -----------------------------------------------------------------------
  uint64_t kernel_end_addr = (uint64_t)&kernel_end;
  if (kernel_end_addr % PAGE_SIZE != 0)
    kernel_end_addr =
        (kernel_end_addr + PAGE_SIZE) & ~(uint64_t)(PAGE_SIZE - 1);
  kprintf("MM: Kernel image ends at 0x%lx. Reserving memory below this.\n",
          0x00FF0000, kernel_end_addr);

  // -----------------------------------------------------------------------
  // Step 4 — Collect module reserved ranges from multiboot tags.
  // -----------------------------------------------------------------------
  struct multiboot_tag_module {
    struct multiboot_tag common;
    uint32_t mod_start;
    uint32_t mod_end;
    char cmdline[];
  } PACKED;

  struct {
    uint64_t start;
    uint64_t end;
  } reserved_ranges[10];
  int reserved_count = 0;

  struct multiboot_tag *mod_tag = (struct multiboot_tag *)(multiboot_addr + 8);
  for (; mod_tag->type != 0;
       mod_tag = (struct multiboot_tag *)((uint8_t *)mod_tag +
                                          ((mod_tag->size + 7) & ~7))) {
    if (mod_tag->type == 3) {
      struct multiboot_tag_module *m = (struct multiboot_tag_module *)mod_tag;
      if (reserved_count < 10) {
        reserved_ranges[reserved_count].start = m->mod_start;
        reserved_ranges[reserved_count].end = m->mod_end;
        kprintf("MM: Reserving module memory: 0x%lx - 0x%lx\n", 0x00FF0000,
                (uint64_t)m->mod_start, (uint64_t)m->mod_end);
        reserved_count++;
      }
    }
  }

  // -----------------------------------------------------------------------
  // Step 5 — Find bitmap_base: the lowest high-memory (>=4GB) physical addr.
  // The main bitmap index is relative to this base so we don't waste bitmap
  // entries on the gap between 4GB and the first actual high-memory region.
  // -----------------------------------------------------------------------
  uint32_t num_entries =
      (mmap_tag->common.size - sizeof(*mmap_tag)) / mmap_tag->entry_size;
  kprintf("MM: Detected %u memory map entries.\n", 0x00FF0000, num_entries);

  bitmap_base = 0xFFFFFFFFFFFFFFFFULL; // sentinel — will be lowered below

  for (uint32_t i = 0; i < num_entries; i++) {
    struct multiboot_mmap_entry *entry = &mmap_tag->entries[i];
    if (entry->type == MULTIBOOT_MEMORY_AVAILABLE &&
        entry->addr >= 0x100000000ULL) {
      uint64_t aligned = entry->addr & ~(uint64_t)(PAGE_SIZE - 1);
      if (aligned < bitmap_base)
        bitmap_base = aligned;
    }
  }

  // If no high memory was found at all, set a safe default.
  if (bitmap_base == 0xFFFFFFFFFFFFFFFFULL)
    bitmap_base = 0x100000000ULL;

  kprintf("MM: Main bitmap base (first high-mem page): 0x%lx\n", 0x00FF0000,
          bitmap_base);

  // -----------------------------------------------------------------------
  // Step 6 — Walk memory map entries and free usable pages into the bitmaps.
  // Skips: kernel image, module regions, page 0.
  // -----------------------------------------------------------------------
  for (uint32_t i = 0; i < num_entries; i++) {
    struct multiboot_mmap_entry *entry = &mmap_tag->entries[i];
    if (entry->type != MULTIBOOT_MEMORY_AVAILABLE)
      continue;

    kprintf("MM: Usable RAM at 0x%lx, size 0x%lx\n", 0x00FF0000, entry->addr,
            entry->len);

    // Align region to page boundaries.
    uint64_t first_page =
        (entry->addr + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t last_page =
        (entry->addr + entry->len) & ~(uint64_t)(PAGE_SIZE - 1);

    // Walk pages in reverse so the allocator hands out lower addresses first.
    for (uint64_t p = last_page; p > first_page;) {
      p -= PAGE_SIZE;

      // Skip page 0 — must never be a valid allocation (catches null deref).
      if (p == 0)
        continue;

      // Skip pages occupied by the kernel image.
      if (p < kernel_end_addr)
        continue;

      // Skip pages occupied by multiboot modules.
      int is_reserved = 0;
      for (int r = 0; r < reserved_count; r++) {
        if (p < reserved_ranges[r].end &&
            (p + PAGE_SIZE) > reserved_ranges[r].start) {
          is_reserved = 1;
          break;
        }
      }
      if (is_reserved)
        continue;

      pfa_free(p); // marks bit as free, increments counters
    }
  }

  kprintf("MM: PFA initialized. Free low pages: %lu, Free high pages: %lu\n",
          0x00FF0000, low_free_count, free_pages_count);
  kprintf("MM: Total usable memory: %lu KB\n", 0x00FF0000,
          mm_get_total_memory() / 1024);
  kprintf("MM: Free memory: %lu KB\n", 0x00FF0000, mm_get_free_memory() / 1024);

  // -----------------------------------------------------------------------
  // Step 7 — Set up MMIO virtual address region in the higher half.
  // -----------------------------------------------------------------------
  next_mmio_addr = 0xFFFFFFFF80000000ULL;
  kprintf("MM: MMIO mapping region starts at 0x%lx\n", 0x00FF0000,
          next_mmio_addr);
}

// ---------------------------------------------------------------------------
// mmio_remap — map a physical MMIO region into virtual address space
// ---------------------------------------------------------------------------

void *mmio_remap(uint64_t physical_addr, size_t size) {
  if (physical_addr < 0x100000000ULL) {
    // Low address — bootloader identity mapping covers this.
    return (void *)physical_addr;
  }

  // High address — create explicit page table mappings.
  kprintf("MM: Mapping high MMIO addr 0x%lx to virt 0x%lx\n", 0x00FF0000,
          physical_addr, next_mmio_addr);

  uint64_t phys_base = physical_addr & ~0xFFFULL;
  uint64_t offset = physical_addr & 0xFFFULL;

  size_t pages_needed = (size + offset + PAGE_SIZE - 1) / PAGE_SIZE;
  if (pages_needed == 0)
    pages_needed = 1;
  if (pages_needed > 64)
    pages_needed = 64; // safety cap

  uint64_t virt_base = next_mmio_addr;

  for (size_t i = 0; i < pages_needed; i++) {
    map_page(phys_base + (i * PAGE_SIZE), virt_base + (i * PAGE_SIZE),
             PAGE_PRESENT | PAGE_RW | PAGE_PCD);
  }

  next_mmio_addr += pages_needed * PAGE_SIZE;

  kprintf("MM: Mapped %u pages at virt 0x%lx\n", 0x00FF0000,
          (uint32_t)pages_needed, virt_base);

  return (void *)(virt_base + offset);
}

// ---------------------------------------------------------------------------
// virt_to_phys — translate a kernel virtual address to physical
// ---------------------------------------------------------------------------

uint64_t virt_to_phys(void *vaddr) {
  uint64_t addr = (uint64_t)vaddr;

  // Higher-half direct map region: subtract the fixed offset.
  if (addr >= DIRECT_MAP_OFFSET)
    return addr - DIRECT_MAP_OFFSET;

  // Low kernel image addresses: identity mapped (virt == phys).
  return addr;
}