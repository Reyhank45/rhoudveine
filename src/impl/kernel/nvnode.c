#include "include/nvnode.h"
#include "include/usb.h"
#include "stdio.h"
#include <stddef.h>
#include <stdint.h>
#include "vray.h"

// Forward declaration for kprintf
extern void kprintf(const char *format, uint32_t color, ...);

#define MAX_NVNODES 32
#define MAX_USB_DEVICES 32

static nvnode_t nvnode_pool[MAX_NVNODES];
static int next_nvnode_index = 0;

static usb_device_t usb_device_pool[MAX_USB_DEVICES];
static int next_usb_device_index = 0;


// Custom implementation of memset
static void *custom_memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n-- > 0) {
        *p++ = (unsigned char)c;
    }
    return s;
}

// Custom implementation of strcpy
static char *custom_strcpy(char *dest, const char *src) {
    char *orig_dest = dest;
    while ((*dest++ = *src++) != '\0');
    return orig_dest;
}

void nvnode_init() {
    custom_memset(nvnode_pool, 0, sizeof(nvnode_pool));
    next_nvnode_index = 0;
    custom_memset(usb_device_pool, 0, sizeof(usb_device_pool));
    next_usb_device_index = 0;
    kprintf("NVNode subsystem initialized.\n", 0x00FF0000);
}

nvnode_t *nvnode_create(nvdevice_type_t type, void *driver_data) {
    if (next_nvnode_index >= MAX_NVNODES) {
        return NULL;
    }

    nvnode_t *new_node = &nvnode_pool[next_nvnode_index++];
    custom_strcpy(new_node->name, "NVNODE");
    new_node->type = type;
    new_node->driver_data = driver_data;
    new_node->next = NULL;

    return new_node;
}

void nvnode_add_usb_device(uint16_t vendor_id, uint16_t product_id) {
    if (next_usb_device_index >= MAX_USB_DEVICES) {
        return; // No more space for USB devices
    }

    usb_device_t* new_usb_dev = &usb_device_pool[next_usb_device_index++];
    new_usb_dev->vendor_id = vendor_id;
    new_usb_dev->product_id = product_id;

    nvnode_create(NVDEVICE_TYPE_USB, new_usb_dev);
}

void nvnode_populate_from_pci() {
    // TODO: This is a placeholder. In the future, this could create
    // nvnodes for PCI devices if needed, similar to vnode_populate_from_pci.
    // For now, USB devices are added via xhci driver, not directly from PCI scan here.
}


void nvnode_dump_list() {
    kprintf("--- NVNode Device List ---\n", 0x00FF0000);
    for (int i = 0; i < next_nvnode_index; i++) {
        nvnode_t *current = &nvnode_pool[i];
        kprintf("  %d: %s (Type: %d)", 0x00FF0000, i, current->name, current->type);
        if (current->type == NVDEVICE_TYPE_USB) {
            usb_device_t* dev = (usb_device_t*)current->driver_data;
            kprintf(" - USB Device: VID=%x, PID=%x\n", 0x00FF0000, dev->vendor_id, dev->product_id);
        } else {
            kprintf("\n", 0x00FF0000);
        }
    }
    kprintf("-------------------------- \n", 0x00FF0000);
}
