#ifndef KERNEL_VNODE_H
#define KERNEL_VNODE_H

#include <stdint.h>

typedef enum {
    DEVICE_TYPE_GENERIC,
    DEVICE_TYPE_PCI,
    DEVICE_TYPE_USB_ROOT,
    DEVICE_TYPE_ACPI,
} device_type_t;

typedef struct vnode {
    char name[32];
    device_type_t type;
    void *driver_data;
    struct vnode *next;
} vnode_t;

void vnode_init();
vnode_t *vnode_create(device_type_t type, void *driver_data);
void vnode_register(vnode_t *node);
void vnode_populate_from_pci();
void vnode_dump_list();

#endif // KERNEL_VNODE_H