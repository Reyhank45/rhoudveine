#include "include/vnode.h"
#include "include/stdio.h"
#include <stddef.h>
#include <stdint.h>
#include "include/autoconf.h"
#include "include/vray.h"

// Forward declaration for kprintf
extern void kprintf(const char *format, uint32_t color, ...);

#define MAX_VNODES 256

static vnode_t vnode_pool[MAX_VNODES];
static int next_vnode_index = 0;

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

void vnode_init() {
    custom_memset(vnode_pool, 0, sizeof(vnode_pool));
    next_vnode_index = 0;
    kprintf("VNode subsystem initialized.\n", 0x00FF0000);
}

vnode_t *vnode_create(device_type_t type, void *driver_data) {
    if (next_vnode_index >= MAX_VNODES) {
        // Out of VNodes
        return NULL;
    }

    vnode_t *new_node = &vnode_pool[next_vnode_index++];

    custom_strcpy(new_node->name, "VNODE");
    
    new_node->type = type;
    new_node->driver_data = driver_data;
    new_node->next = NULL; // This field is not really used with a static array, but we keep it for the struct definition

    return new_node;
}

// This function is no longer needed with a static array, but we keep it for API compatibility.
void vnode_register(vnode_t *node) {
    // With a static pool, registration is implicit in creation.
    // This function can be a no-op.
    if (!node) return;
}

void vnode_populate_from_pci() {
    #ifdef CONFIG_VRAY
    const struct vray_device *devices = vray_devices();
    int count = vray_device_count();

    for (int i = 0; i < count; i++) {
        const struct vray_device *dev = &devices[i];
        // Create VNodes for AHCI and xHCI controllers
        if ((dev->class == 0x01 && dev->subclass == 0x06) || // AHCI Controller
            (dev->class == 0x0C && dev->subclass == 0x03 && dev->prog_if == 0x30)) { // xHCI Controller
            vnode_create(DEVICE_TYPE_GENERIC, (void *)dev);
        }
    }
    #endif
}


void vnode_dump_list() {
    kprintf("--- VNode Device List ---\n", 0x00FF0000);
    for (int i = 0; i < next_vnode_index; i++) {
        vnode_t *current = &vnode_pool[i];
        kprintf("  %d: %s (Type: %d)", 0x00FF0000, i, current->name, current->type);
        // All VNodes created from PCI devices will have their driver_data pointing to a vray_device
        #ifdef CONFIG_VRAY
        struct vray_device *dev = (struct vray_device *)current->driver_data;
        if (dev && dev->name) {
            kprintf(" - %s\n", 0x00FF0000, dev->name);
        } else {
            kprintf("\n", 0x00FF0000);
        }
        #else
        kprintf("\n", 0x00FF0000);
        #endif
    }
    kprintf("-------------------------\n", 0x00FF0000);
}
