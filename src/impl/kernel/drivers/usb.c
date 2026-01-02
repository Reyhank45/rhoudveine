#include "include/usb.h"
#include "include/xhci.h"
#include "include/stdio.h"

extern void kprintf(const char *format, uint32_t color, ...);

void usb_init() {
    kprintf("USB subsystem initialization.\n", 0x00FF0000);

    // Initialize host controller drivers. For now, just xHCI.
    xhci_init();
}