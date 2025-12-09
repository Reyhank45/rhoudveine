// Kernel panic shell interface
#ifndef RH_PANIC_H
#define RH_PANIC_H

/* Print an interactive panic shell and machine state dump to the framebuffer.
 * `reason` is a short string describing why the panic handler was invoked.
 */
void kernel_panic_shell(const char *reason);

#endif
