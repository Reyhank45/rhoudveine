#ifndef KERNEL_USB_H
#define KERNEL_USB_H

#include <stdint.h>

typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    // more fields to be added here, e.g., device address, class, etc.
} usb_device_t;

void usb_init();

#endif // KERNEL_USB_H