#include "include/xhci.h"
#include "vray.h"
#include "include/nvnode.h"
#include "include/io.h"
#include "console.h"
#include "stdio.h"
#include <stddef.h>
#include "include/mm.h"
#include <stdint.h> // Include for standard integer types

// Forward declaration for kprintf from main.c
extern void kprintf(const char *format, uint32_t color, ...);
extern uint64_t virt_to_phys(void* vaddr);

// --- xHCI Register Structures (Spec 5.3) ---
typedef volatile struct __attribute__((packed)) {
    uint8_t caplength;
    uint8_t res1;
    uint16_t hciversion;
    uint32_t hcsparams1;
    uint32_t hcsparams2;
    uint32_t hcsparams3;
    uint32_t hccparams1;
    uint32_t dboff;
    uint32_t rtsoff;
    uint32_t hccparams2;
} xhci_cap_regs_t;

typedef volatile struct __attribute__((packed)) {
    uint32_t usbcmd;
    uint32_t usbsts;
    uint32_t pagesize;
    uint32_t res1[2];
    uint32_t dnctrl;
    uint64_t crcr;
    uint32_t res2[4];
    uint64_t dcbaap;
    uint32_t config;
    // Port Status and Control Registers (PORTSC) start here.
    // We access them via pointer arithmetic from the base of op_regs.
} xhci_op_regs_t;

typedef volatile struct __attribute__((packed)) {
    uint32_t mfindex;
    // ... other runtime registers
} xhci_runtime_regs_t;

// Transfer Request Block (TRB) types
enum {
    TRB_TYPE_NORMAL = 1,
    TRB_TYPE_SETUP_STAGE = 2,
    TRB_TYPE_DATA_STAGE = 3,
    TRB_TYPE_STATUS_STAGE = 4,
    TRB_TYPE_ISOCH = 5,
    TRB_TYPE_LINK = 6,
    TRB_TYPE_EVENT_DATA = 7,
    TRB_TYPE_NO_OP = 8,
    // Command TRB types
    TRB_TYPE_ENABLE_SLOT = 9,
    TRB_TYPE_DISABLE_SLOT = 10,
    TRB_TYPE_ADDRESS_DEVICE = 11,
    TRB_TYPE_CONFIGURE_ENDPOINT = 12,
    TRB_TYPE_EVALUATE_CONTEXT = 13,
    TRB_TYPE_RESET_ENDPOINT = 14,
    TRB_TYPE_STOP_ENDPOINT = 15,
    TRB_TYPE_SET_TR_DEQUEUE = 16,
    TRB_TYPE_RESET_DEVICE = 17,
    TRB_TYPE_FORCE_EVENT = 18,
    TRB_TYPE_NEGOTIATE_BANDWIDTH = 19,
    TRB_TYPE_SET_LATENCY_TOLERANCE = 20,
    TRB_TYPE_GET_PORT_BANDWIDTH = 21,
    TRB_TYPE_FORCE_HEADER = 22,
    TRB_TYPE_NO_OP_COMMAND = 23,
    // Event TRB types
    TRB_TYPE_TRANSFER_EVENT = 32,
    TRB_TYPE_COMMAND_COMPLETION_EVENT = 33,
    TRB_TYPE_PORT_STATUS_CHANGE_EVENT = 34,
    TRB_TYPE_BANDWIDTH_REQUEST_EVENT = 35,
    TRB_TYPE_DOORBELL_EVENT = 36,
    TRB_TYPE_HOST_CONTROLLER_EVENT = 37,
    TRB_TYPE_DEVICE_NOTIFICATION_EVENT = 38,
    TRB_TYPE_MFINDEX_WRAP_EVENT = 39,
} xhci_trb_type;

// Generic TRB structure (16 bytes)
typedef struct __attribute__((packed)) {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} xhci_trb_t;

// Event Ring Segment Table (ERST) Entry (Spec 6.5)
typedef struct __attribute__((packed)) {
    uint64_t ring_segment_base_addr;
    uint32_t ring_segment_size;
    uint32_t rsvd;
} xhci_erst_entry_t;

// --- xHCI Data Structures (Spec 6.2) ---

// Slot Context (32 bytes)
typedef struct __attribute__((packed)) {
    uint32_t dwords[8];
} xhci_slot_context_t;

// Endpoint Context (32 bytes)
typedef struct __attribute__((packed)) {
    uint32_t dwords[8];
} xhci_endpoint_context_t;

// Device Context: 1 Slot Context + 31 Endpoint Contexts (1024 bytes)
typedef struct __attribute__((packed, aligned(64))) {
    xhci_slot_context_t slot;
    xhci_endpoint_context_t eps[31];
} xhci_device_context_t;

// Input Context: 1 Input Control Context + 1 Slot Context + 31 Endpoint Contexts (1056 bytes)
typedef struct __attribute__((packed, aligned(64))) {
    uint32_t drop_flags;
    uint32_t add_flags;
    uint32_t rsvd[6];
    xhci_slot_context_t slot;
    xhci_endpoint_context_t eps[31];
} xhci_input_context_t;

// Ring sizes
#define XHCI_CMD_RING_SIZE 32
#define XHCI_EVENT_RING_SIZE 32
#define XHCI_MAX_SLOTS 256
#define XHCI_DCBAA_SIZE (XHCI_MAX_SLOTS + 1) // Max 256 device slots + 1 (index 0 is reserved)
#define XHCI_EP_RING_SIZE 32

// Statically allocated memory for xHCI structures (must be 64-byte aligned)
// __attribute__((aligned(64))) ensures 64-byte alignment
static __attribute__((aligned(64))) xhci_trb_t xhci_cmd_ring[XHCI_CMD_RING_SIZE];
static __attribute__((aligned(64))) xhci_erst_entry_t xhci_event_ring_seg_table[1]; // Event Ring Segment Table (ERST)
static __attribute__((aligned(64))) xhci_trb_t xhci_event_ring[XHCI_EVENT_RING_SIZE];
static __attribute__((aligned(64))) uint64_t xhci_dcbaap_array[XHCI_DCBAA_SIZE]; // Device Context Base Address Array
static __attribute__((aligned(64))) xhci_device_context_t xhci_device_context_pool[XHCI_MAX_SLOTS + 1];
static __attribute__((aligned(64))) xhci_input_context_t xhci_input_context_pool[XHCI_MAX_SLOTS + 1];
static __attribute__((aligned(64))) xhci_trb_t xhci_ep_transfer_rings[XHCI_MAX_SLOTS + 1][XHCI_EP_RING_SIZE];

// Custom memset for clearing memory, required for static allocations
static void *custom_memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n-- > 0) {
        *p++ = (unsigned char)c;
    }
    return s;
}

// Forward declaration
static void *custom_memset(void *s, int c, size_t n);


// Pointer to the xHCI Capability Registers
static xhci_cap_regs_t *xhci_cap_regs = NULL;
static xhci_op_regs_t *xhci_op_regs = NULL;
static volatile uint32_t *xhci_doorbell_regs = NULL;
static xhci_runtime_regs_t *xhci_runtime_regs = NULL;

// Command Ring Management
static uint32_t cmd_ring_enqueue_ptr = 0;
static uint32_t cmd_ring_cycle_state = 1; // Initial Cycle State is 1

// Event Ring Management
static uint32_t event_ring_dequeue_ptr = 0;
static uint32_t event_ring_cycle_state = 1; // Initial Cycle State is 1

static void xhci_address_device(uint32_t slot_id, uint32_t port_id, uint32_t port_speed);

// Function to send a command to the xHCI controller
// This is a blocking function that waits for the command to complete.
// completion_code_out: receives the completion code from the event.
// slot_id_out: receives the slot ID from the event (for Enable Slot command).
static int xhci_send_command(xhci_trb_t *command_trb, uint32_t *completion_code_out, uint32_t *slot_id_out) {
    // Check if the command ring is full
    if (((cmd_ring_enqueue_ptr + 1) % XHCI_CMD_RING_SIZE) == 0) {
        kprintf("xHCI: Command Ring is full!\n", 0xFF0000);
        return -1; // Ring full
    }

    // Copy the command TRB to the command ring
    xhci_cmd_ring[cmd_ring_enqueue_ptr].parameter = command_trb->parameter; // 64-bit
    xhci_cmd_ring[cmd_ring_enqueue_ptr].status = command_trb->status;
    xhci_cmd_ring[cmd_ring_enqueue_ptr].control = command_trb->control;

    // Set Cycle Bit
    if (cmd_ring_cycle_state) {
        xhci_cmd_ring[cmd_ring_enqueue_ptr].control |= (1 << 0); // Set Cycle Bit
    } else {
        xhci_cmd_ring[cmd_ring_enqueue_ptr].control &= ~(1 << 0); // Clear Cycle Bit
    }

    // Advance enqueue pointer
    cmd_ring_enqueue_ptr = (cmd_ring_enqueue_ptr + 1) % XHCI_CMD_RING_SIZE;
    if (cmd_ring_enqueue_ptr == 0) {
        cmd_ring_cycle_state = !cmd_ring_cycle_state; // Toggle Cycle State
        // A Link TRB should be placed at the end of the ring to link back to the beginning.
        // For simplicity, we assume a single segment and handle wrapping implicitly for now.
    }

    // Ring the Command Doorbell (Doorbell Register 0)
    xhci_doorbell_regs[0] = 0; // Target Doorbell 0 (Host Controller), value 0 (no slot ID)
    kprintf("xHCI: Command sent. Waiting for completion...\n", 0x00FF0000);

    // Wait for Command Completion Event on the Event Ring
    int timeout = 1000000; // ~1 second timeout
    while (timeout > 0) {
        xhci_trb_t *event_trb = &xhci_event_ring[event_ring_dequeue_ptr];

        // Check Cycle Bit
        uint32_t event_cycle_bit = (event_trb->control & (1 << 0)) ? 1 : 0;

        if (event_cycle_bit == event_ring_cycle_state) {
            // Event received!
            uint32_t trb_type = (event_trb->control >> 10) & 0x3F;
            uint32_t completion_code = (event_trb->status >> 24) & 0xFF;

            // Advance event ring dequeue pointer
            event_ring_dequeue_ptr = (event_ring_dequeue_ptr + 1) % XHCI_EVENT_RING_SIZE;
            if (event_ring_dequeue_ptr == 0) {
                event_ring_cycle_state = !event_ring_cycle_state; // Toggle Cycle State
            }
            // Update ERDP (Event Ring Dequeue Pointer) register
            volatile uint64_t *erdp = (uint64_t*)((uint8_t*)xhci_runtime_regs + 0x38);
            *erdp = virt_to_phys(&xhci_event_ring[event_ring_dequeue_ptr]) | (1 << 3);

            if (trb_type == TRB_TYPE_COMMAND_COMPLETION_EVENT) {
                uint32_t slot_id = (event_trb->control >> 24) & 0xFF;
                kprintf("xHCI: Command Completion Event received (Slot: %u, Code: %u)\n", 0x00FF0000, slot_id, completion_code);

                if (completion_code_out) *completion_code_out = completion_code;
                if (slot_id_out) *slot_id_out = slot_id;

                if (completion_code == 1) { // 1 = Success
                    return 0; // Success
                } else {
                    return -2; // Command error
                }
            } else {
                kprintf("xHCI: Ignoring event of type %u while waiting for command completion.\n", 0xFFFF00, trb_type);
                // This was not the event we were looking for. Continue waiting.
            }
        }

        for (volatile int i = 0; i < 100; i++); // Simple busy-wait
        timeout--;
    }

    kprintf("xHCI: Command Completion Event timeout!\n", 0xFF0000);
    return -1; // Timeout error
}

void xhci_init() {
    // Find the xHCI controller
    // Specifically look for class 0x0C, subclass 0x03, and prog_if 0x30 for xHCI
    int device_index = vray_find_first_by_class_prog_if(0x0C, 0x03, 0x30);
    const struct vray_device *xhci_controller = NULL;
    if (device_index != -1) {
        xhci_controller = &vray_devices()[device_index];
    }
    

    if (xhci_controller) {
        kprintf("xHCI controller found at %x:%x.%d\n", 0x00FF0000,
               xhci_controller->bus, xhci_controller->device, xhci_controller->function);

        // Enable PCI Bus Mastering for the xHCI controller
        uint32_t pci_command_reg = vray_cfg_read(xhci_controller->bus, xhci_controller->device, xhci_controller->function, 0x04);
        pci_command_reg |= (1 << 2) | (1 << 1); // Set Bus Master Enable AND Memory Space Enable
        vray_cfg_write(xhci_controller->bus, xhci_controller->device, xhci_controller->function, 0x04, pci_command_reg);

        // Read BAR0 (Base Address Register 0) from the PCI config space.
        // This is typically at offset 0x10.
        uint32_t bar0 = vray_cfg_read(xhci_controller->bus, xhci_controller->device, xhci_controller->function, 0x10);

        // Determine if it's a 64-bit BAR.
        // If bit 0 is 0, it's a memory BAR. If bit 1 is 1 (Type field), it's 64-bit.
        // Type field (bits 2:1) is 0b10 for 64-bit. So bar0 & 0x7 should be 0x4.
        uint64_t base_addr = 0;
        if ((bar0 & 0x7) == 0x4) { // 64-bit memory BAR
            uint32_t bar1 = vray_cfg_read(xhci_controller->bus, xhci_controller->device, xhci_controller->function, 0x14);
            base_addr = ((uint64_t)bar1 << 32) | (bar0 & 0xFFFFFFF0);
        } else if ((bar0 & 0x01) == 0) { // 32-bit memory BAR
            base_addr = (uint64_t)(bar0 & 0xFFFFFFF0);
        } else {
            kprintf("xHCI: BAR0 is not a memory space BAR. Cannot proceed.\n", 0xFF0000);
            return;
        }

        kprintf("xHCI: Base Address (physical) = 0x%lx\n", 0x00FF0000, base_addr);

        // Assume identity mapping for now.
        // The physical address must be mapped to a virtual address.
        // We'll map a 64KB region to be safe.
        void* virt_base = mmio_remap(base_addr, 64 * 1024);
        if (!virt_base) {
            kprintf("xHCI: Failed to map MMIO region.\n", 0xFF0000);
            return;
        }
        kprintf("xHCI: MMIO region mapped to virtual address 0x%lx\n", 0x00FF0000, (uint64_t)virt_base);

        xhci_cap_regs = (xhci_cap_regs_t *)virt_base;

        // Verify basic capability register access
        kprintf("xHCI: CAPLENGTH = %u, HCIVERSION = 0x%x\n", 0x00FF0000, xhci_cap_regs->caplength, xhci_cap_regs->hciversion);

        // Calculate the address of operational registers
        xhci_op_regs = (xhci_op_regs_t *)((uint8_t*)virt_base + xhci_cap_regs->caplength);

        // Calculate the address of doorbell registers
        xhci_doorbell_regs = (uint32_t *)((uint8_t*)virt_base + xhci_cap_regs->dboff);

        // Calculate the address of runtime registers
        xhci_runtime_regs = (xhci_runtime_regs_t *)((uint8_t*)virt_base + xhci_cap_regs->rtsoff);

        // --- Host Controller Reset (HCRST) ---
        kprintf("xHCI: Performing HCRST...\n", 0x00FF0000);

        // Set HCRST bit (bit 1) in USBCMD
        xhci_op_regs->usbcmd |= (1 << 1);

        // Wait for HCRST bit to clear
        // And wait for CNR (Controller Not Ready) bit (bit 11) in USBSTS to clear
        int hcrst_timeout = 1000000; // ~1 second timeout
        while ((xhci_op_regs->usbcmd & (1 << 1)) || (xhci_op_regs->usbsts & (1 << 11))) {
            // Simple busy-wait loop for now. In a real OS, this would be a proper delay.
            for (volatile int i = 0; i < 100; i++); 
            hcrst_timeout--;
            if (hcrst_timeout <= 0) {
                kprintf("xHCI: HCRST timeout! USBCMD=0x%x, USBSTS=0x%x\n", 0xFF0000, xhci_op_regs->usbcmd, xhci_op_regs->usbsts);
                return;
            }
        }
        kprintf("xHCI: HCRST complete.\n", 0x00FF0000);

        // Initialize the DCBAA (Device Context Base Address Array)
        custom_memset(xhci_dcbaap_array, 0, sizeof(xhci_dcbaap_array));
        custom_memset(xhci_input_context_pool, 0, sizeof(xhci_input_context_pool));
        custom_memset(xhci_device_context_pool, 0, sizeof(xhci_device_context_pool));
        custom_memset(xhci_ep_transfer_rings, 0, sizeof(xhci_ep_transfer_rings));

        // Set the DCBAA Address Register
        xhci_op_regs->dcbaap = virt_to_phys(xhci_dcbaap_array);
        kprintf("xHCI: DCBAAP physical address set to 0x%lx\n", 0x00FF0000, xhci_op_regs->dcbaap);

        // --- Initialize Command Ring ---
        kprintf("xHCI: Initializing Command Ring...\n", 0x00FF0000);
        custom_memset(xhci_cmd_ring, 0, sizeof(xhci_cmd_ring));
        
        // Set Command Ring Control Register (CRCR)
        // Point to the physical address of the Command Ring, set RCS (Ring Cycle State) to 1.
        // The LSB (bit 0) of CRCR is the Command Stop (CS) bit, which should be 0 during normal operation.
        // Bit 1 is the Ring Cycle State (RCS) bit. We set it to our cycle state.
        xhci_op_regs->crcr = virt_to_phys(xhci_cmd_ring) | (1 << 1); // Set RCS to 1
        kprintf("xHCI: Command Ring physical address set to 0x%lx\n", 0x00FF0000, virt_to_phys(xhci_cmd_ring));

        // --- Initialize Event Ring ---
        kprintf("xHCI: Initializing Event Ring...\n", 0x00FF0000);
        custom_memset(xhci_event_ring, 0, sizeof(xhci_event_ring));
        custom_memset(xhci_event_ring_seg_table, 0, sizeof(xhci_event_ring_seg_table));

        // Setup the single entry in the Event Ring Segment Table (ERST)
        xhci_event_ring_seg_table[0].ring_segment_base_addr = virt_to_phys(xhci_event_ring);
        xhci_event_ring_seg_table[0].ring_segment_size = XHCI_EVENT_RING_SIZE;
        xhci_event_ring_seg_table[0].rsvd = 0;

        // Runtime registers are at an offset from the start of BAR0
        // These are part of the first Interrupter Register Set
        volatile uint32_t *erstsz = (uint32_t*)((uint8_t*)xhci_runtime_regs + 0x28); // ERST Size
        volatile uint64_t *erstba = (uint64_t*)((uint8_t*)xhci_runtime_regs + 0x30); // ERST Base Address
        volatile uint64_t *erdp = (uint64_t*)((uint8_t*)xhci_runtime_regs + 0x38);

        // Per xHCI Spec 4.9.3, the initialization order is critical:
        // 1. Set ERST Size
        // 2. Set ERDP
        // 3. Set ERST Base Address

        // Set ERST Size Register (ERSTSZ)
        *erstsz = 1; // 1 segment
        // Set Event Ring Dequeue Pointer (ERDP) to the start of the ring
        *erdp = virt_to_phys(xhci_event_ring); // EHB is implicitly 0
        // Set ERST Base Address Register (ERSTBA)
        *erstba = virt_to_phys(xhci_event_ring_seg_table);

        kprintf("xHCI: ERSTBA set to 0x%lx, ERSTSZ set to %u\n", 0x00FF0000, *erstba, *erstsz);
        kprintf("xHCI: ERDP set to 0x%lx\n", 0x00FF0000, *erdp);

        // --- Configure xHCI operational registers ---
        kprintf("xHCI: Configuring operational registers...\n", 0x00FF0000);

        // Get Max Device Slots from HCSPARAMS1 (bits 24:31)
        uint32_t max_slots = xhci_cap_regs->hcsparams1 & 0xFF;
        // Set Max Device Slots Enabled (MDSE) in CONFIG_REG (bits 0:7)
        xhci_op_regs->config = max_slots;
        kprintf("xHCI: Max Device Slots Enabled set to %u\n", 0x00FF0000, max_slots);

        // Enable Interrupts (INTE bit 2) in USBCMD
        xhci_op_regs->usbcmd |= (1 << 2);
        kprintf("xHCI: Interrupts enabled.\n", 0x00FF0000);

        // --- Start the xHCI controller ---
        kprintf("xHCI: Starting controller...\n", 0x00FF0000);
        // Set Run/Stop bit (bit 0) in USBCMD
        xhci_op_regs->usbcmd |= (1 << 0);

        // Wait for Controller Halted (HCH) bit (bit 0) in USBSTS to clear
        int hch_timeout = 1000000; // ~1 second timeout
        while (xhci_op_regs->usbsts & (1 << 0)) { // HCH bit is 0 when running
            // Simple busy-wait loop for now.
            for (volatile int i = 0; i < 100; i++); 
            hch_timeout--;
            if (hch_timeout <= 0) {
                kprintf("xHCI: Controller start timeout! USBSTS=0x%x\n", 0xFF0000, xhci_op_regs->usbsts);
                return;
            }
        }
        kprintf("xHCI: Controller started successfully.\n", 0x00FF0000);

        // --- Scan Root Hub Ports ---
        kprintf("xHCI: Scanning root hub ports...\n", 0x00FF0000);

        // Get MaxPorts from HCSPARAMS1 (bits 24:31) (already extracted as max_slots above)
        uint32_t max_ports = (xhci_cap_regs->hcsparams1 >> 24) & 0xFF; // MaxPorts is in the same field as MaxSlots
        kprintf("xHCI: Number of ports: %u\n", 0x00FF0000, max_ports);

        for (uint32_t i = 0; i < max_ports; i++) {
            // Port numbers are 1-indexed in xHCI
            uint32_t port_id = i + 1;
            volatile uint32_t *portsc_reg = (uint32_t*)((uint8_t*)xhci_op_regs + 0x400 + (i * 0x10));
            uint32_t portsc_val = *portsc_reg;

            kprintf("xHCI: Port %u: PORTSC = 0x%x\n", 0x00FF0000, port_id, portsc_val);

            // Check if a device is connected (CCS - Current Connect Status, bit 0)
            if (portsc_val & (1 << 0)) { // Current Connect Status
                kprintf("xHCI: Device connected to Port %d.\n", 0x00FF0000, port_id);

                // If Port Power (PP - bit 9) is off, turn it on.
                if (!(portsc_val & (1 << 9))) {
                    kprintf("xHCI: Powering on Port %d...\n", 0x00FF0000, port_id);
                    *portsc_reg = portsc_val | (1 << 9); // Set PP bit
                    // Read back to ensure write took effect and allow time
                    for (volatile int delay = 0; delay < 1000; delay++); 
                    portsc_val = *portsc_reg;
                    kprintf("xHCI: Port %d PORTSC after power on: 0x%x\n", 0x00FF0000, port_id, portsc_val);
                }

                // Reset the port (PR - bit 4)
                kprintf("xHCI: Resetting Port %d...\n", 0x00FF0000, port_id);
                // The mask 0x0E00C3E0 is incorrect. To set a R/WS bit like Port Reset,
                // we can simply OR it with the current value, as writing 0 to R/WC bits has no effect.
                *portsc_reg |= (1 << 4); // Set Port Reset bit

                // Wait for Port Reset Change (PRC - bit 21) to be set, indicating reset complete
                int port_reset_timeout = 1000000; // ~1 second timeout
                while (!(*portsc_reg & (1 << 21))) {
                    for (volatile int delay = 0; delay < 100; delay++);
                    port_reset_timeout--;
                    if (port_reset_timeout <= 0) {
                        kprintf("xHCI: Port %d reset timeout!\n", 0xFF0000, port_id);
                        break;
                    }
                }
                if (port_reset_timeout <= 0) continue;

                kprintf("xHCI: Port %u reset complete. PORTSC=0x%x\n", 0x00FF0000, port_id, *portsc_reg);
                
                // Clear Port Reset Change (PRC - bit 21) by writing 1 to it
                // The mask is incorrect. Just write 1 to the R/WC bit.
                *portsc_reg |= (1 << 21);

                // Port now should be enabled (PED - bit 1) and have a speed.
                // The speed is in bits 10:13 (Port Speed - PS)
                uint32_t port_speed = (*portsc_reg >> 10) & 0xF;
                kprintf("xHCI: Port %u enabled. Speed ID: %u\n", 0x00FF0000, port_id, port_speed);
                
                // --- Device Enumeration for this Port ---
                // Step 1: Enable Slot Command
                xhci_trb_t enable_slot_cmd;
                custom_memset(&enable_slot_cmd, 0, sizeof(xhci_trb_t));
                enable_slot_cmd.control = (TRB_TYPE_ENABLE_SLOT << 10); // TRB Type = Enable Slot Command
                kprintf("xHCI: Sending Enable Slot Command for Port %u\n", 0x00FF0000, port_id);
                
                uint32_t completion_code = 0;
                uint32_t slot_id = 0;
                if (xhci_send_command(&enable_slot_cmd, &completion_code, &slot_id) == 0) {
                    kprintf("xHCI: Enable Slot successful for Port %u. Allocated Slot ID: %u\n", 0x00FF0000, port_id, slot_id);
                    // Step 2: Address Device Command
                    xhci_address_device(slot_id, port_id, port_speed);
                } else {
                    kprintf("xHCI: Enable Slot Command failed for Port %u. Code: %u\n", 0xFF0000, port_id, completion_code);
                }

            }

            // Clear any status change bits by writing 1 to them
            // CSC (bit 17), PEC (bit 18), PRC (bit 19), OCC (bit 20), WRC (bit 21), CLC (bit 22), CEC (bit 23)
            // The mask 0x0E00C3E0 is incorrect. To clear R/WC bits, we write 1s to them.
            // A read-modify-write is safest to preserve other writable bits.
            uint32_t clear_mask = (1 << 17) | (1 << 18) | (1 << 19) | (1 << 20) | (1 << 21) | (1 << 22) | (1 << 23);
            *portsc_reg |= clear_mask;
        }

    } else {
        kprintf("No xHCI controller found.\n", 0xFF0000);
    }
}

static void xhci_address_device(uint32_t slot_id, uint32_t port_id, uint32_t port_speed) {
    kprintf("xHCI: Addressing device in Slot %u (Port %u)\n", 0x00FF0000, slot_id, port_id);

    // 1. Get pointers to the device's contexts
    xhci_device_context_t *dev_ctx = &xhci_device_context_pool[slot_id];
    xhci_input_context_t *input_ctx = &xhci_input_context_pool[slot_id];
    custom_memset(dev_ctx, 0, sizeof(xhci_device_context_t));
    custom_memset(input_ctx, 0, sizeof(xhci_input_context_t));

    // 2. Set the DCBAAP entry for this slot
    xhci_dcbaap_array[slot_id] = virt_to_phys(dev_ctx);

    // 3. Setup Input Context for Address Device command
    // Input Control Context: A0=1 (Slot), A1=1 (EP0)
    input_ctx->add_flags = (1 << 1) | (1 << 0);

    // Slot Context (in Input Context)
    // DWord 0: Route String, Speed, etc.
    // Route String = 0 for root hub ports.
    // Context Entries = 1 (for EP0).
    input_ctx->slot.dwords[0] = (port_speed << 20) | (1 << 27);
    // DWord 1: Root Hub Port Number
    input_ctx->slot.dwords[1] = (port_id << 16);

    // Endpoint 0 Context (in Input Context)
    // DWord 1: EP Type = Control (4), Max Packet Size
    uint16_t max_packet_size = 0;
    switch (port_speed) {
        case 1: max_packet_size = 8; break;  // Full-speed
        case 2: max_packet_size = 8; break;  // Low-speed
        case 3: max_packet_size = 64; break; // High-speed
        case 4: max_packet_size = 512; break;// SuperSpeed
        default: max_packet_size = 64; break; // Default
    }
    input_ctx->eps[0].dwords[1] = (4 << 3) | (max_packet_size << 16); // EP0 is a control endpoint

    // DWord 2 & 3: TR Dequeue Pointer
    uint64_t ep0_ring_addr = virt_to_phys(&xhci_ep_transfer_rings[slot_id][0]);
    // Set DCS (Dequeue Cycle State) to 1
    ep0_ring_addr |= 1;
    *((uint64_t*)&input_ctx->eps[0].dwords[2]) = ep0_ring_addr;

    // DWord 4: Average TRB Length
    input_ctx->eps[0].dwords[4] = 8; // For control endpoints

    kprintf("xHCI: Input Context @ 0x%lx, Device Context @ 0x%lx\n", 0x00FF0000, (uint64_t)input_ctx, (uint64_t)dev_ctx);
    kprintf("xHCI: DCBAAP[%u] = 0x%lx\n", 0x00FF0000, slot_id, xhci_dcbaap_array[slot_id]);

    // 4. Create and send Address Device command
    xhci_trb_t addr_dev_cmd;
    custom_memset(&addr_dev_cmd, 0, sizeof(xhci_trb_t));
    addr_dev_cmd.parameter = virt_to_phys(input_ctx);
    addr_dev_cmd.control = (slot_id << 24) | (TRB_TYPE_ADDRESS_DEVICE << 10);

    uint32_t completion_code;
    if (xhci_send_command(&addr_dev_cmd, &completion_code, NULL) == 0) {
        kprintf("xHCI: Address Device successful for Slot %u. Device is now in Addressed state.\n", 0x00FF0000, slot_id);
        // Add a placeholder nvnode for this device.
        // We don't know the VID/PID yet, that requires control transfers.
        nvnode_add_usb_device(0, 0);
    } else {
        kprintf("xHCI: Address Device failed for Slot %u. Code: %u\n", 0xFF0000, slot_id, completion_code);
    }
}