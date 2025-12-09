/* Legacy init stub removed — replaced with harmless no-op stub.
 * The active init implementation is now `init/init.c` at project root.
 */

typedef void (*print_fn_t)(const char*);

void main(print_fn_t print) {
    /* No-op fallback: return immediately. If the kernel somehow
     * calls this symbol, it will simply halt to avoid confusing output.
     */
    (void)print;
    for (;;) { __asm__ volatile ("cli; hlt"); }
}
