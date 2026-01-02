#include "include/pci_db.h"
#include "include/pci_db_generated.h"
#include <stddef.h>

const char *get_pci_device_name(uint16_t vendor_id, uint16_t device_id) {
    int left = 0;
    int right = PCI_DB_SIZE - 1;

    while (left <= right) {
        int mid = left + (right - left) / 2;
        const pci_device_entry_t *entry = &pci_device_db[mid];

        if (entry->vendor_id == vendor_id) {
            if (entry->device_id == device_id) {
                return entry->name;
            } else if (entry->device_id < device_id) {
                left = mid + 1;
            } else {
                right = mid - 1;
            }
        } else if (entry->vendor_id < vendor_id) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    
    return "Unknown Device";
}
