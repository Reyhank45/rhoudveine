#ifndef PCI_DB_H
#define PCI_DB_H

#include <stdint.h>

const char *get_pci_device_name(uint16_t vendor_id, uint16_t device_id);

#endif
