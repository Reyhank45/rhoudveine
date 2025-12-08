#pragma once

#include <stdint.h>

// Load an ELF64 image from memory and jump to its entry point.
// `image` points to ELF bytes and `size` is the length in bytes.
// `print_fn` is a pointer that will be passed in RDI to the entry point.
// Returns 0 on success (does not return if the program runs), negative on error.
int elf64_load_and_run(void *image, uint32_t size, void (*print_fn)(const char*));
