#include <stdint.h>
#include "elf.h"
#include "console.h"

// Minimal ELF64 loader: only supports ET_EXEC or ET_DYN with PT_LOAD segments.
// Assumes identity mapping for low physical addresses (loader runs in long mode
// with identity mapping created earlier).

struct elf64_hdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define PT_LOAD 1

static uint64_t min_u64(uint64_t a, uint64_t b) { return a < b ? a : b; }

int elf64_load_and_run(void *image, uint32_t size, void (*print_fn)(const char*)) {
    if (!image || size < sizeof(struct elf64_hdr)) return -1;
    uint8_t *data = (uint8_t*)image;
    struct elf64_hdr *eh = (struct elf64_hdr*)data;
    if (eh->e_ident[EI_MAG0] != ELFMAG0 || eh->e_ident[EI_MAG1] != ELFMAG1 ||
        eh->e_ident[EI_MAG2] != ELFMAG2 || eh->e_ident[EI_MAG3] != ELFMAG3) {
        return -2;
    }

    // iterate program headers
    struct elf64_phdr *ph = (struct elf64_phdr*)(data + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph->p_type == PT_LOAD) {
            // bounds checks
            if (ph->p_offset + ph->p_filesz > size) return -3;
            uint8_t *src = data + ph->p_offset;
            uint8_t *dst = (uint8_t*)(uintptr_t)(ph->p_paddr ? ph->p_paddr : ph->p_vaddr);
            // copy file data
            for (uint64_t k = 0; k < ph->p_filesz; k++) dst[k] = src[k];
            // zero the rest
            for (uint64_t k = ph->p_filesz; k < ph->p_memsz; k++) dst[k] = 0;
        }
        ph++;
    }

    // Prepare to jump: print a debug message then jump to e_entry with RDI=print_fn
    fb_puts("Jumping to init entry\n");
    // Create function pointer to entry
    void (*entry)(void) = (void(*)(void))(uintptr_t)eh->e_entry;

    // Jump: set RDI to print_fn and jump to entry
    __asm__ volatile (
        "mov %0, %%rdi\n"
        "jmp *%1\n"
        : /* no outputs */
        : "r"(print_fn), "r"(entry)
        : "rdi"
    );

    return 0; // never reached
}
