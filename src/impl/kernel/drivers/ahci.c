#include "include/ahci.h"
#include "include/vray.h"
#include "include/mm.h"
#include "include/stdio.h"
#include <stdint.h>
#include <stddef.h>

// Forward declarations
extern void kprintf(const char *format, uint32_t color, ...);

// Local memset implementation
static void custom_memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n-- > 0) {
        *p++ = (unsigned char)c;
    }
}

// AHCI HBA (Host Bus Adapter) Memory Registers
typedef volatile struct {
    uint32_t cap;        // 0x00: Host capabilities
    uint32_t ghc;        // 0x04: Global host control
    uint32_t is;         // 0x08: Interrupt status
    uint32_t pi;         // 0x0C: Ports implemented
    uint32_t vs;         // 0x10: Version
    uint32_t ccc_ctl;    // 0x14: Command completion coalescing control
    uint32_t ccc_ports;  // 0x18: Command completion coalescing ports
    uint32_t em_loc;     // 0x1C: Enclosure management location
    uint32_t em_ctl;     // 0x20: Enclosure management control
    uint32_t cap2;       // 0x24: Host capabilities extended
    uint32_t bohc;       // 0x28: BIOS/OS handoff control and status
    uint8_t  rsv[0xA0 - 0x2C];  // Reserved
    uint8_t  vendor[0x100 - 0xA0]; // Vendor specific
} ahci_hba_mem_t;

// AHCI Port Registers
typedef volatile struct {
    uint32_t clb;        // 0x00: Command list base address (low)
    uint32_t clbu;       // 0x04: Command list base address (high)
    uint32_t fb;         // 0x08: FIS base address (low)
    uint32_t fbu;        // 0x0C: FIS base address (high)
    uint32_t is;         // 0x10: Interrupt status
    uint32_t ie;         // 0x14: Interrupt enable
    uint32_t cmd;        // 0x18: Command and status
    uint32_t rsv0;       // 0x1C: Reserved
    uint32_t tfd;        // 0x20: Task file data
    uint32_t sig;        // 0x24: Signature
    uint32_t ssts;       // 0x28: Serial ATA status (SCR0: SStatus)
    uint32_t sctl;       // 0x2C: Serial ATA control (SCR2: SControl)
    uint32_t serr;       // 0x30: Serial ATA error (SCR1: SError)
    uint32_t sact;       // 0x34: Serial ATA active (SCR3: SActive)
    uint32_t ci;         // 0x38: Command issue
    uint32_t sntf;       // 0x3C: Serial ATA notification
    uint32_t fbs;        // 0x40: FIS-based switching control
    uint32_t rsv1[11];   // 0x44-0x6F: Reserved
    uint32_t vendor[4];  // 0x70-0x7F: Vendor specific
} ahci_hba_port_t;

// FIS (Frame Information Structure) Types
#define FIS_TYPE_REG_H2D    0x27  // Register FIS - host to device
#define FIS_TYPE_REG_D2H    0x34  // Register FIS - device to host
#define FIS_TYPE_DMA_ACT    0x39  // DMA activate FIS
#define FIS_TYPE_DMA_SETUP  0x41  // DMA setup FIS
#define FIS_TYPE_DATA       0x46  // Data FIS
#define FIS_TYPE_BIST       0x58  // BIST activate FIS
#define FIS_TYPE_PIO_SETUP  0x5F  // PIO setup FIS
#define FIS_TYPE_DEV_BITS   0xA1  // Set device bits FIS

// FIS - Register Host to Device
typedef struct {
    uint8_t  fis_type;   // FIS_TYPE_REG_H2D
    uint8_t  pm_port:4;  // Port multiplier
    uint8_t  rsv0:3;     // Reserved
    uint8_t  c:1;        // 1: Command, 0: Control
    uint8_t  command;    // Command register
    uint8_t  featurel;   // Feature register, 7:0
    
    uint8_t  lba0;       // LBA low register, 7:0
    uint8_t  lba1;       // LBA mid register, 15:8
    uint8_t  lba2;       // LBA high register, 23:16
    uint8_t  device;     // Device register
    
    uint8_t  lba3;       // LBA register, 31:24
    uint8_t  lba4;       // LBA register, 39:32
    uint8_t  lba5;       // LBA register, 47:40
    uint8_t  featureh;   // Feature register, 15:8
    
    uint16_t count;      // Count (sector count)
    uint8_t  icc;        // Isochronous command completion
    uint8_t  control;    // Control register
    
    uint8_t  rsv1[4];    // Reserved
} __attribute__((packed)) fis_reg_h2d_t;

// Command Header
typedef struct {
    uint8_t  cfl:5;      // Command FIS length in DWORDs
    uint8_t  a:1;        // ATAPI
    uint8_t  w:1;        // Write
    uint8_t  p:1;        // Prefetchable
    
    uint8_t  r:1;        // Reset
    uint8_t  b:1;        // BIST
    uint8_t  c:1;        // Clear busy upon R_OK
    uint8_t  rsv0:1;     // Reserved
    uint8_t  pmp:4;      // Port multiplier port
    
    uint16_t prdtl;      // Physical region descriptor table length
    uint32_t prdbc;      // Physical region descriptor byte count
    uint32_t ctba;       // Command table base address (low)
    uint32_t ctbau;      // Command table base address (high)
    uint32_t rsv1[4];    // Reserved
} __attribute__((packed)) ahci_cmd_header_t;

// Physical Region Descriptor Table Entry
typedef struct {
    uint32_t dba;        // Data base address (low)
    uint32_t dbau;       // Data base address (high)
    uint32_t rsv0;       // Reserved
    uint32_t dbc:22;     // Byte count (4M max)
    uint32_t rsv1:9;     // Reserved
    uint32_t i:1;        // Interrupt on completion
} __attribute__((packed)) ahci_prdt_entry_t;

// Command Table
typedef struct {
    uint8_t  cfis[64];   // Command FIS
    uint8_t  acmd[16];   // ATAPI command
    uint8_t  rsv[48];    // Reserved
    ahci_prdt_entry_t prdt_entry[1]; // Physical region descriptor table (we'll use 1)
} __attribute__((packed)) ahci_cmd_table_t;

// ATA Commands
#define ATA_CMD_READ_DMA_EX     0x25
#define ATA_CMD_WRITE_DMA_EX    0x35
#define ATA_CMD_IDENTIFY        0xEC

// Port Commands
#define HBA_PORT_CMD_ST     0x0001
#define HBA_PORT_CMD_FRE    0x0010
#define HBA_PORT_CMD_FR     0x4000
#define HBA_PORT_CMD_CR     0x8000

// Device Detection
#define AHCI_DEV_NULL    0
#define AHCI_DEV_SATA    1
#define AHCI_DEV_SATAPI  2
#define AHCI_DEV_SEMB    3
#define AHCI_DEV_PM      4

// Global state
static ahci_hba_mem_t *g_abar = NULL;
static int g_ahci_initialized = 0;
static int g_ahci_port = -1;  // First valid SATA port

// Check device type
static int check_type(ahci_hba_port_t *port) {
    uint32_t ssts = port->ssts;
    uint8_t det = ssts & 0xF;
    uint8_t ipm = (ssts >> 8) & 0xF;
    
    if (det != 3 || ipm != 1) return AHCI_DEV_NULL;
    
    uint32_t sig = port->sig;
    switch (sig) {
        case 0xEB140101: return AHCI_DEV_SATAPI;
        case 0xC33C0101: return AHCI_DEV_SEMB;
        case 0x96690101: return AHCI_DEV_PM;
        default:         return AHCI_DEV_SATA;
    }
}

// Stop command engine
static void port_stop_cmd(ahci_hba_port_t *port) {
    port->cmd &= ~HBA_PORT_CMD_ST;
    port->cmd &= ~HBA_PORT_CMD_FRE;
    
    // Wait until FR (FIS receive running) and CR (Command list running) are cleared
    int timeout = 100000;
    while (timeout-- > 0) {
        if ((port->cmd & HBA_PORT_CMD_FR) == 0 && (port->cmd & HBA_PORT_CMD_CR) == 0)
            break;
    }
}

// Start command engine
static void port_start_cmd(ahci_hba_port_t *port) {
    // Wait until CR (command list running) is cleared
    int timeout = 100000;  
    while ((port->cmd & HBA_PORT_CMD_CR) != 0 && timeout-- > 0);
    
    port->cmd |= HBA_PORT_CMD_FRE;
    port->cmd |= HBA_PORT_CMD_ST;
}

// Find a free command slot
static int find_cmdslot(ahci_hba_port_t *port) {
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++) {
        if ((slots & (1 << i)) == 0)
            return i;
    }
    return -1;
}

// Read sectors from port
static int port_read(ahci_hba_port_t *port, uint64_t lba, uint32_t count, uint8_t *buffer) {
    port->is = (uint32_t)-1; // Clear pending interrupts
    
    int slot = find_cmdslot(port);
    if (slot == -1) {
        kprintf("AHCI: No free command slots\n", 0xFFFF0000);
        return -1;
    }
    
    // Get command list (physical address stored in port->clb)
    ahci_cmd_header_t *cmdheader = (ahci_cmd_header_t*)(uintptr_t)(uint64_t)port->clb;
    cmdheader += slot;
    cmdheader->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmdheader->w = 0; // Read
    cmdheader->prdtl = 1;
    
    // Command table (physical address)
    ahci_cmd_table_t *cmdtbl = (ahci_cmd_table_t*)(uintptr_t)(uint64_t)cmdheader->ctba;
    custom_memset(cmdtbl, 0, sizeof(ahci_cmd_table_t));
    
    // Setup PRDT
    cmdtbl->prdt_entry[0].dba = (uint32_t)(uintptr_t)buffer;
    cmdtbl->prdt_entry[0].dbau = (uint32_t)((uintptr_t)buffer >> 32);
    cmdtbl->prdt_entry[0].dbc = (count * 512) - 1; // 512 bytes per sector, 0-based
    cmdtbl->prdt_entry[0].i = 0;
    
    // Setup command FIS
    fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t*)(&cmdtbl->cfis);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1; // Command
    cmdfis->command = ATA_CMD_READ_DMA_EX;
    
    cmdfis->lba0 = (uint8_t)lba;
    cmdfis->lba1 = (uint8_t)(lba >> 8);
    cmdfis->lba2 = (uint8_t)(lba >> 16);
    cmdfis->lba3 = (uint8_t)(lba >> 24);
    cmdfis->lba4 = (uint8_t)(lba >> 32);
    cmdfis->lba5 = (uint8_t)(lba >> 40);
    
    cmdfis->device = 1 << 6; // LBA mode
    cmdfis->count = (uint16_t)count;
    
    // Wait for port to be ready
    int timeout = 100000;
    while ((port->tfd & (0x80 | 0x08)) && timeout-- > 0);
    
    if (timeout == 0) {
        kprintf("AHCI: Port hung\n", 0xFFFF0000);
        return -1;
    }
    
    // Issue command
    port->ci = 1 << slot;
    
    // Wait for completion
    timeout = 1000000;
    while (timeout-- > 0) {
        if ((port->ci & (1 << slot)) == 0) break;
        if (port->is & (1 << 30)) { // Task file error
            kprintf("AHCI: Task file error\n", 0xFFFF0000);
            return -1;
        }
    }
    
    if (timeout == 0) {
        kprintf("AHCI: Read timeout\n", 0xFFFF0000);
        return -1;
    }
    
    if (port->is & (1 << 30)) {
        kprintf("AHCI: Task file error after completion\n", 0xFFFF0000);
        return -1;
    }
    
    return 0;
}

// Write sectors to port
static int port_write(ahci_hba_port_t *port, uint64_t lba, uint32_t count, const uint8_t *buffer) {
    port->is = (uint32_t)-1; // Clear pending interrupts
    
    int slot = find_cmdslot(port);
    if (slot == -1) {
        kprintf("AHCI: No free command slots\n", 0xFFFF0000);
        return -1;
    }
    
    // Get command list
    ahci_cmd_header_t *cmdheader = (ahci_cmd_header_t*)(uintptr_t)(uint64_t)port->clb;
    cmdheader += slot;
    cmdheader->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmdheader->w = 1; // Write
    cmdheader->prdtl = 1;
    
    // Command table
    ahci_cmd_table_t *cmdtbl = (ahci_cmd_table_t*)(uintptr_t)(uint64_t)cmdheader->ctba;
    custom_memset(cmdtbl, 0, sizeof(ahci_cmd_table_t));
    
    // Setup PRDT
    cmdtbl->prdt_entry[0].dba = (uint32_t)(uintptr_t)buffer;
    cmdtbl->prdt_entry[0].dbau = (uint32_t)((uintptr_t)buffer >> 32);
    cmdtbl->prdt_entry[0].dbc = (count * 512) - 1;
    cmdtbl->prdt_entry[0].i = 0;
    
    // Setup command FIS
    fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t*)(&cmdtbl->cfis);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1; // Command
    cmdfis->command = ATA_CMD_WRITE_DMA_EX;
    
    cmdfis->lba0 = (uint8_t)lba;
    cmdfis->lba1 = (uint8_t)(lba >> 8);
    cmdfis->lba2 = (uint8_t)(lba >> 16);
    cmdfis->lba3 = (uint8_t)(lba >> 24);
    cmdfis->lba4 = (uint8_t)(lba >> 32);
    cmdfis->lba5 = (uint8_t)(lba >> 40);
    
    cmdfis->device = 1 << 6; // LBA mode
    cmdfis->count = (uint16_t)count;
    
    // Wait for port to be ready
    int timeout = 100000;
    while ((port->tfd & (0x80 | 0x08)) && timeout-- > 0);
    
    if (timeout == 0) {
        kprintf("AHCI: Port hung\n", 0xFFFF0000);
        return -1;
    }
    
    // Issue command
    port->ci = 1 << slot;
    
    // Wait for completion
    timeout = 1000000;
    while (timeout-- > 0) {
        if ((port->ci & (1 << slot)) == 0) break;
        if (port->is & (1 << 30)) {
            kprintf("AHCI: Task file error\n", 0xFFFF0000);
            return -1;
        }
    }
    
    if (timeout == 0) {
        kprintf("AHCI: Write timeout\n", 0xFFFF0000);
        return -1;
    }
    
    if (port->is & (1 << 30)) {
        kprintf("AHCI: Task file error after completion\n", 0xFFFF0000);
        return -1;
    }
    
    return 0;
}

int ahci_init(void) {
    kprintf("AHCI: Initializing AHCI driver...\n", 0x00FF0000);
    
    // Find AHCI controller (class 0x01, subclass 0x06)
    int ahci_dev_idx = vray_find_first_by_class(0x01, 0x06);
    if (ahci_dev_idx < 0) {
        kprintf("AHCI: No AHCI controller found\n", 0xFFFF0000);
        return -1;
    }
    
    const struct vray_device *devices = vray_devices();
    const struct vray_device *dev = &devices[ahci_dev_idx];
    
    kprintf("AHCI: Found AHCI controller at %d:%d.%d\n", 0x00FF0000, 
            dev->bus, dev->device, dev->function);
    
    // Read BAR5 (AHCI Base Address Register)
    uint32_t bar5_low = vray_cfg_read(dev->bus, dev->device, dev->function, 0x24);
    uint32_t bar5_high = vray_cfg_read(dev->bus, dev->device, dev->function, 0x28);
    
    uint64_t abar_phys = (uint64_t)(bar5_low & 0xFFFFFFF0) | ((uint64_t)bar5_high << 32);
    
    kprintf("AHCI: ABAR physical address: 0x%lx\n", 0x00FF0000, abar_phys);
    
    // Map AHCI registers (they're already identity mapped in our setup)
    g_abar = (ahci_hba_mem_t*)(uintptr_t)abar_phys;
    
    kprintf("AHCI: AHCI version: 0x%x\n", 0x00FF0000, g_abar->vs);
    kprintf("AHCI: Ports implemented: 0x%x\n", 0x00FF0000, g_abar->pi);
    
    // Probe ports
    uint32_t pi = g_abar->pi;
    int port_count = 0;
    
    for (int i = 0; i < 32; i++) {
        if ((pi >> i) & 1) {
            ahci_hba_port_t *port = (ahci_hba_port_t*)((uint8_t*)g_abar + 0x100 + (i * 0x80));
            int dt = check_type(port);
            
            if (dt == AHCI_DEV_SATA) {
                kprintf("AHCI: SATA drive found at port %d\n", 0x00FFFF00, i);
                if (g_ahci_port == -1) {
                    g_ahci_port = i;
                    
                    // Stop command engine
                    port_stop_cmd(port);
                    
                    // Allocate command list (1KB, 32 entries * 32 bytes)
                    uint8_t *cmd_list = (uint8_t*)pfa_alloc();
                    if (!cmd_list) {
                        kprintf("AHCI: Failed to allocate command list\n", 0xFFFF0000);
                        return -1;
                    }
                    custom_memset(cmd_list, 0, 4096);
                    
                    // Allocate FIS area (256 bytes)
                    uint8_t *fis = (uint8_t*)pfa_alloc();
                    if (!fis) {
                        kprintf("AHCI: Failed to allocate FIS\n", 0xFFFF0000);
                        return -1;
                    }
                    custom_memset(fis, 0, 4096);
                    
                    // Allocate command tables (need at least 256 bytes per entry, let's use 1 page for 1 entry)
                    uint8_t *cmd_table = (uint8_t*)pfa_alloc();
                    if (!cmd_table) {
                        kprintf("AHCI: Failed to allocate command table\n", 0xFFFF0000);
                        return -1;
                    }
                    custom_memset(cmd_table, 0, 4096);
                    
                    port->clb = (uint32_t)(uintptr_t)cmd_list;
                    port->clbu = (uint32_t)((uintptr_t)cmd_list >> 32);
                    port->fb = (uint32_t)(uintptr_t)fis;
                    port->fbu = (uint32_t)((uintptr_t)fis >> 32);
                    
                    // Set command table address in command header
                    ahci_cmd_header_t *cmdheader = (ahci_cmd_header_t*)cmd_list;
                    cmdheader->ctba = (uint32_t)(uintptr_t)cmd_table;
                    cmdheader->ctbau = (uint32_t)((uintptr_t)cmd_table >> 32);
                    
                    // Start command engine
                    port_start_cmd(port);
                    
                    kprintf("AHCI: Port %d initialized\n", 0x00FFFF00, i);
                }
                port_count++;
            } else if (dt == AHCI_DEV_SATAPI) {
                kprintf("AHCI: SATAPI drive at port %d (not supported)\n", 0xFFFF00FF, i);
            }
        }
    }
    
    if (g_ahci_port == -1) {
        kprintf("AHCI: No usable SATA drive found\n", 0xFFFF0000);
        return -1;
    }
    
    g_ahci_initialized = 1;
    kprintf("AHCI: Initialization complete\n", 0x00FF0000);
    return 0;
}

int ahci_read_sectors(uint64_t lba, uint32_t count, uint8_t *buffer) {
    if (!g_ahci_initialized || g_ahci_port == -1) {
        kprintf("AHCI: Not initialized\n", 0xFFFF0000);
        return -1;
    }
    
    ahci_hba_port_t *port = (ahci_hba_port_t*)((uint8_t*)g_abar + 0x100 + (g_ahci_port * 0x80));
    return port_read(port, lba, count, buffer);
}

int ahci_write_sectors(uint64_t lba, uint32_t count, const uint8_t *buffer) {
    if (!g_ahci_initialized || g_ahci_port == -1) {
        kprintf("AHCI: Not initialized\n", 0xFFFF0000);
        return -1;
    }
    
    ahci_hba_port_t *port = (ahci_hba_port_t*)((uint8_t*)g_abar + 0x100 + (g_ahci_port * 0x80));
    return port_write(port, lba, count, buffer);
}

int ahci_get_port_count(void) {
    if (!g_ahci_initialized) return 0;
    return (g_ahci_port >= 0) ? 1 : 0;
}

int ahci_is_initialized(void) {
    return g_ahci_initialized;
}
