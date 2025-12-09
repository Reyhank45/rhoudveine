/* VRAY - basic PCI config-space access and scanning
 * This module provides a minimal PCI enumerator using IO ports 0xCF8/0xCFC
 * and exposes a small device table for drivers to use.
 */

#include <stdint.h>
#include "vray.h"
// kernel print helpers
extern void kprint(const char *str, uint32_t color);
extern void kprintf(const char *format, uint32_t color, ...);

#include "stdio.h"

// I/O port helpers (inline)
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a" (val), "Nd" (port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t val;
    __asm__ volatile ("inl %1, %0" : "=a" (val) : "Nd" (port));
    return val;
}

// config address ports
#define VRAY_CONF_ADDR 0xCF8
#define VRAY_CONF_DATA 0xCFC

// internal device store
static struct vray_device devices[256];
static int dev_count = 0;

// Build config address
static uint32_t vray_build_addr(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) {
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)device << 11) | ((uint32_t)func << 8) | (offset & 0xFC);
    return addr;
}

uint32_t vray_cfg_read(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) {
    uint32_t addr = vray_build_addr(bus, device, func, offset);
    outl(VRAY_CONF_ADDR, addr);
    return inl(VRAY_CONF_DATA + (offset & 0x3));
}

void vray_cfg_write(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t addr = vray_build_addr(bus, device, func, offset);
    outl(VRAY_CONF_ADDR, addr);
    outl(VRAY_CONF_DATA + (offset & 0x3), value);
}

// Simple scan: bus 0, devices 0..31, functions 0..7
void vray_init(void) {
    dev_count = 0;
    for (uint8_t device = 0; device < 32; device++) {
        for (uint8_t function = 0; function < 8; function++) {
            uint32_t v = vray_cfg_read(0, device, function, 0);
            uint16_t vendor = (uint16_t)(v & 0xFFFF);
            uint16_t device_id = (uint16_t)((v >> 16) & 0xFFFF);
            if (vendor == 0xFFFF || vendor == 0x0000) {
                // no device
                if (function == 0) break; // if function 0 absent, no more functions
                continue;
            }

            uint32_t cls = vray_cfg_read(0, device, function, 8);
            uint8_t class_code = (cls >> 24) & 0xFF;
            uint8_t subclass = (cls >> 16) & 0xFF;
            uint8_t prog_if = (cls >> 8) & 0xFF;
            uint32_t hdr = vray_cfg_read(0, device, function, 0x0C);
            uint8_t header_type = (hdr >> 16) & 0xFF;
            uint32_t irq = vray_cfg_read(0, device, function, 0x3C);

            if (dev_count < (int)(sizeof(devices)/sizeof(devices[0]))) {
                devices[dev_count].bus = 0;
                devices[dev_count].device = device;
                devices[dev_count].function = function;
                devices[dev_count].vendor_id = vendor;
                devices[dev_count].device_id = device_id;
                devices[dev_count].class = class_code;
                devices[dev_count].subclass = subclass;
                devices[dev_count].prog_if = prog_if;
                devices[dev_count].header_type = header_type;
                devices[dev_count].irq = (uint8_t)(irq & 0xFF);
                dev_count++;
            }

            // Print a short summary via kernel print
            char buf[128];
            // Using kprintf is not declared here; use simple fb_puts/kprintf via stdio.h
            kprintf("VRAY: found device at %d:%d.%d vendor=0x%x device=0x%x class=0x%x\n", 0x00FF0000, 0, device, function, vendor, device_id, class_code);

            // if function 0 and header type indicates single function, skip other functions
            if (function == 0 && ((header_type & 0x80) == 0)) break;
        }
    }
}

int vray_find_first_by_vendor(uint16_t vendor_id, uint16_t device_id) {
    for (int i = 0; i < dev_count; i++) {
        if (devices[i].vendor_id == vendor_id && devices[i].device_id == device_id) return i;
    }
    return -1;
}

int vray_find_first_by_class(uint8_t class, uint8_t subclass) {
    for (int i = 0; i < dev_count; i++) {
        if (devices[i].class == class && devices[i].subclass == subclass) return i;
    }
    return -1;
}

const struct vray_device* vray_devices(void) { return devices; }
int vray_device_count(void) { return dev_count; }
