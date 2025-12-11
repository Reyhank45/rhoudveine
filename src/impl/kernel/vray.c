/* VRAY - basic PCI config-space access and scanning
 * This module provides a minimal PCI enumerator using IO ports 0xCF8/0xCFC
 * and exposes a small device table for drivers to use.
 */

#include <stdint.h>
#include "vray.h"
#include <stddef.h>
// kernel print helpers
extern void kprint(const char *str, uint32_t color);
extern void kprintf(const char *format, uint32_t color, ...);

#include "stdio.h"

// PCI device name database entry
typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    const char *name;
} pci_device_name_t;

// A small, hardcoded database of common PCI devices
static const pci_device_name_t pci_device_names[] = {
    // QEMU/VirtualBox Devices
    {0x8086, 0x100E, "Intel Pro/1000 Network Adapter (QEMU)"},
    {0x1234, 0x1111, "QEMU Virtual VGA Controller"},
    {0x1AF4, 0x1000, "Virtio network card"},
    {0x1b36, 0x000d, "QEMU xHCI Host Controller"},
    {0x1b36, 0x0001, "QEMU Standard VGA Adapter"},
    {0x80EE, 0xCAFE, "VirtualBox Graphics Adapter"},

    // Devices from Lenovo IdeaPad Slim 3 (Intel Comet Lake)
    {0x8086, 0x9b71, "Intel Host bridge"},
    {0x8086, 0x9b41, "Intel UHD Graphics (CometLake-U GT2)"},
    {0x8086, 0x1903, "Intel Thermal Subsystem"},
    {0x8086, 0x1911, "Intel Gaussian Mixture Model"},
    {0x8086, 0x02f9, "Intel Comet Lake Thermal Subsystem"},
    {0x8086, 0x02ed, "Intel Comet Lake PCH-LP USB 3.1 xHCI Controller"},
    {0x8086, 0x02ef, "Intel Comet Lake PCH-LP Shared SRAM"},
    {0x8086, 0x02f0, "Intel Comet Lake PCH-LP CNVi WiFi"},
    {0x8086, 0x02e8, "Intel Serial IO I2C Host Controller"},
    {0x8086, 0x02e0, "Intel Comet Lake Management Engine Interface"},
    {0x8086, 0x02d3, "Intel Comet Lake SATA AHCI Controller"},
    {0x8086, 0x02c5, "Intel Comet Lake Serial IO I2C Host Controller"},
    {0x8086, 0x02c7, "Intel Comet Lake Device 02c7"},
    {0x8086, 0x02b4, "Intel Comet Lake PCI Express Root Port"},
    {0x8086, 0x0284, "Intel Comet Lake PCH-LP LPC/eSPI Controller"},
    {0x8086, 0x02c8, "Intel Comet Lake PCH-LP cAVS Audio"},
    {0x8086, 0x02a3, "Intel Comet Lake PCH-LP SMBus Host Controller"},
    {0x8086, 0x02a4, "Intel Comet Lake SPI (flash) Controller"},
    {0x1e95, 0x9100, "SSSTC CL1-3D256-Q11 NVMe SSD"},

    // Common Intel Devices
    {0x8086, 0x2922, "Intel ICH9R/DO/DH SATA AHCI Controller"},
    {0x8086, 0x153a, "Intel I217-V Gigabit Network Connection"},

    // Common AMD devices
    {0x1002, 0x731f, "AMD Navi 21 [Radeon RX 6800/6800 XT / 6900 XT]"},
    {0x1002, 0x1638, "AMD Cezanne [Radeon Vega Series / Radeon Vega Mobile Series]"},
    {0x1022, 0x1435, "AMD Starship/Matisse Root Complex"},
    {0x1022, 0x148a, "AMD Starship/Matisse PCIe Dummy Host Bridge"},
    {0x1022, 0x790b, "AMD FCH SMBus Controller"},
    {0x1022, 0x1657, "AMD Navi 10-24 Audio Device"},
    {0x1022, 0x2000, "AMD PCnet-PCI II"},
    {0x1022, 0x1483, "AMD Zen 3 Ryzen SMU"},
    {0x1022, 0x1630, "AMD Radeon RX Vega"},
    {0x1022, 0x145f, "AMD Starship/Matisse HD Audio Controller"},
    {0x1022, 0x43f4, "AMD FCH SATA Controller [AHCI mode]"},

    // Common Realtek Devices
    {0x10EC, 0x8139, "Realtek RTL-8139 Fast Ethernet NIC"},
    {0x10EC, 0x8168, "Realtek RTL8111/8168/8411 PCIe Gigabit Ethernet"},
    {0x10ec, 0x8136, "Realtek RTL810xE PCI Express Fast Ethernet controller"},
    {0x10ec, 0x5286, "Realtek RTS5286 PCI Express Card Reader"},
    {0x10ec, 0x0282, "Realtek RTL8188EE Wireless Network Adapter"},
    {0x10ec, 0x5289, "Realtek RTS5229 PCI Express Card Reader"},

    // Common NVIDIA devices
    {0x10de, 0x1f08, "NVIDIA GeForce RTX 2070"},

    // Common MediaTek devices
    {0x14c3, 0x7961, "MediaTek MT7921 802.11ax Wireless NIC"},
    {0x14c3, 0x7663, "MediaTek MT7663 802.11ac wireless controller"},

    // Common ASUS devices
    {0x1043, 0x8769, "ASUS Xonar D2X Audio Device"},

    // Devices from Asus Vivobook Go 15 (AMD Mendocino)
    {0x1002, 0x164e, "AMD Radeon 610M Graphics"},
    {0x1002, 0x1506, "AMD Mendocino/Ryzen 7020 IOMMU"},
    {0x14c3, 0x7902, "MediaTek MT7902 Wi-Fi 6E"},
    {0x1022, 0x15e4, "AMD ACP/ACP3X/ACP6x Audio Coprocessor"},
    {0x1022, 0x15e5, "AMD ACP-I2S Audio Device"},
    {0x1022, 0x15e8, "AMD Mendocino Navigation and IO Hub"},
    {0x1022, 0x15e9, "AMD Mendocino Control and Power Management"},
    {0x1022, 0x15ea, "AMD Mendocino PMF"},
    {0x1022, 0x15eb, "AMD Mendocino SMU"},
    {0x1022, 0x1605, "AMD Zen 2 NB/IO"},
    {0x1022, 0x1606, "AMD Zen 2 NB/IO"},
    {0x1022, 0x1607, "AMD Zen 2 NB/IO"},
    {0x1022, 0x1608, "AMD Zen 2 NB/IO"},
    {0x1002, 0x67FF, "AMD Ellesmere HDMI Audio [Radeon RX 470/480/570/580/590]"},
    {0x14c3, 0x7922, "MediaTek MT7922 Wi-Fi 6E"},
    {0x10ec, 0xb723, "Realtek RTL8723BE PCIe Wireless Network Adapter"},

    {0, 0, NULL} // End of list
};

// Function to get the name of a PCI device
static const char *get_pci_device_name(uint16_t vendor_id, uint16_t device_id) {
    for (int i = 0; pci_device_names[i].name != NULL; i++) {
        if (pci_device_names[i].vendor_id == vendor_id && pci_device_names[i].device_id == device_id) {
            return pci_device_names[i].name;
        }
    }
    return "Unknown Device";
}


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
    // A dummy read can act as a barrier to ensure the address write has completed
    // before we read the data port.
    (void)inl(VRAY_CONF_ADDR);
    return inl(VRAY_CONF_DATA);
}

void vray_cfg_write(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t addr = vray_build_addr(bus, device, func, offset);
    outl(VRAY_CONF_ADDR, addr);
    // A dummy read can act as a barrier to ensure the address write has completed
    // before we write the data port.
    (void)inl(VRAY_CONF_ADDR);
    outl(VRAY_CONF_DATA, value);
}

// Simple scan: bus 0, devices 0..31, functions 0..7
void vray_init(void) {
    dev_count = 0;
    kprintf("VRAY: Starting PCI bus scan...\n", 0x00FF0000);
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
                devices[dev_count].name = get_pci_device_name(vendor, device_id);
                dev_count++;
            }
            
            kprintf("VRAY: %d:%d.%d [%x:%x] %s (class %x, subclass %x)\n", 0x00FF0000, 0, device, function, vendor, device_id, devices[dev_count-1].name, class_code, subclass);

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

int vray_find_first_by_class_prog_if(uint8_t class, uint8_t subclass, uint8_t prog_if) {
    for (int i = 0; i < dev_count; i++) {
        if (devices[i].class == class && devices[i].subclass == subclass && devices[i].prog_if == prog_if) {
            return i;
        }
    }
    return -1;
}

const struct vray_device* vray_devices(void) { return devices; }
int vray_device_count(void) { return dev_count; }
