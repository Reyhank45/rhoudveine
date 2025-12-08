#include <stdint.h>

// Signature: main receives a single argument: a print function pointer
typedef void (*print_fn_t)(const char*);

void main(print_fn_t print) {
    if (print) print("hello world\n");
    // Halt forever
    for (;;) { __asm__ volatile ("cli; hlt"); }
}
