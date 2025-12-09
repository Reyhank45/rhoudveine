// VRAY - PCI
#ifndef RH_VRAY_H
#define RH_VRAY_H

#include <stdint.h>

struct vray_device {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t header_type;
    uint8_t irq;
};

// Initialize the VRAY subsystem and scan for devices on the root bus.
void vray_init(void);

// Read 32-bit config word from device (bus, device, function, offset)
uint32_t vray_cfg_read(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
// Write 32-bit config word
void vray_cfg_write(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value);

// Find first device matching vendor/device or class/subclass. Returns index or -1
int vray_find_first_by_vendor(uint16_t vendor_id, uint16_t device_id);
int vray_find_first_by_class(uint8_t class, uint8_t subclass);

// Return pointer to internal device array (read-only)
const struct vray_device* vray_devices(void);
int vray_device_count(void);

#endif
