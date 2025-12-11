#ifndef KERNEL_NVNODE_H
#define KERNEL_NVNODE_H

#include <stdint.h>

// Forward declaration to avoid circular dependency
struct vray_device;

typedef enum {
    NVDEVICE_TYPE_GENERIC,
    NVDEVICE_TYPE_USB,
} nvdevice_type_t;

typedef struct nvnode {
    char name[32];
    nvdevice_type_t type;
    void *driver_data;
    struct nvnode *next;
} nvnode_t;

void nvnode_init();
nvnode_t *nvnode_create(nvdevice_type_t type, void *driver_data);
void nvnode_add_usb_device(uint16_t vendor_id, uint16_t product_id);
void nvnode_dump_list();
void nvnode_populate_from_pci();

#endif // KERNEL_NVNODE_H