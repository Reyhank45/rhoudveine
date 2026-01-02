#include "include/xhci.h"
#include "include/vray.h"
#include "include/nvnode.h"
#include "include/io.h"
#include "include/console.h"
#include "include/stdio.h"
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

// USB Keyboard state
static uint32_t usb_kbd_slot = 0;
static uint16_t usb_kbd_max_packet = 8;
static uint32_t usb_kbd_port_speed = 0;
static uint32_t usb_kbd_port_id = 0;
static int usb_kbd_ep1_configured = 0;

// EP1 (keyboard interrupt IN) ring management
static __attribute__((aligned(64))) xhci_trb_t usb_kbd_ep1_ring[XHCI_EP_RING_SIZE];
static uint32_t usb_kbd_ep1_enqueue = 0;
static uint32_t usb_kbd_ep1_cycle = 1;

// Buffer for receiving HID report
static __attribute__((aligned(64))) uint8_t usb_kbd_report_data[8];

// Forward declarations
static void xhci_address_device(uint32_t slot_id, uint32_t port_id, uint32_t port_speed);
static void xhci_configure_kbd_endpoint(uint32_t slot_id, uint32_t port_speed);
static void xhci_queue_kbd_transfer(void);
static void xhci_set_boot_protocol(uint32_t slot_id);

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

// Perform BIOS Handoff to take ownership of the controller
static void xhci_perform_bios_handoff(volatile xhci_cap_regs_t *cap_regs, uintptr_t mmio_base) {
    uint32_t hccparams1 = cap_regs->hccparams1;
    uint32_t xecp = (hccparams1 >> 16) & 0xFFFF;
    
    if (xecp == 0) {
        kprintf("xHCI: No Extended Capabilities found in HCCPARAMS1.\n", 0x00FF0000);
        return;
    }
    
    kprintf("xHCI: Checking Extended Capabilities at offset 0x%x (Dwords)\n", 0x00FF0000, xecp);
    
    // xECP is offset in 32-bit words from MMIO base
    uintptr_t current_cap_addr = mmio_base + (xecp * 4);
    
    while (1) {
        volatile uint32_t *cap_header = (volatile uint32_t*)current_cap_addr;
        uint32_t val = *cap_header;
        uint32_t cap_id = val & 0xFF;
        uint32_t next_offset = (val >> 8) & 0xFF; // In Dwords, relative to current cap
        
        if (cap_id == 1) { // USB Legacy Support
            kprintf("xHCI: Found USB Legacy Support capability at 0x%lx\n", 0x00FF0000, current_cap_addr);
            
            volatile uint32_t *usblegsup = cap_header;
            
            // Check if BIOS owns it
            if (*usblegsup & (1 << 16)) {
                kprintf("xHCI: BIOS owns the controller. Requesting ownership...\n", 0x00FF0000);
                
                // Set OS Owned Semaphore (bit 24)
                *usblegsup |= (1 << 24);
                
                // Wait for BIOS Owned Semaphore (bit 16) to clear
                // Timeout logic needed (usually fairly quick)
                int timeout = 100000; // Arbitrary loop count
                while ((*usblegsup & (1 << 16)) && timeout > 0) {
                    timeout--;
                    for (volatile int i = 0; i < 100; i++);
                }
                
                if (timeout > 0) {
                    kprintf("xHCI: BIOS Handoff successful.\n", 0x00FF0000);
                } else {
                    kprintf("xHCI: BIOS Handoff timed out! Forcing takeover.\n", 0xFF0000);
                    // Disable BIOS Owned Semaphore anyway to be safe? Spec says we should just proceed.
                    *usblegsup &= ~(1 << 16); // Try to force clear it?
                }
            } else {
                kprintf("xHCI: OS already owns the controller.\n", 0x00FF0000);
                // Ensure OS Owned bit is set
                *usblegsup |= (1 << 24);
            }
            
            // Disable Legacy Support by clearing bits 23:1 (SMI enable bits)
            // Preserve header and OS Owned bit
            // *usblegsup &= 0xFF0000FF; // Clear middle bits? 
            // Better to handle SMI/SMI enable registers at offset +4 called USBLEGCTL
            volatile uint32_t *usblegctl = (uint32_t*)(current_cap_addr + 4);
            *usblegctl &= 0; // Clear SMI enables
            
            break; // Found it
        }
        
        if (next_offset == 0) break;
        current_cap_addr += (next_offset * 4);
    }
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

        // --- Perform BIOS Handoff ---
        xhci_perform_bios_handoff(xhci_cap_regs, (uintptr_t)virt_base);

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
        
        // --- Configure Max Device Slots (REQUIRED for Enable Slot to work) ---
        uint32_t hcsparams1 = xhci_cap_regs->hcsparams1;
        uint32_t max_slots = hcsparams1 & 0xFF;
        kprintf("xHCI: Max Slots supported: %u\n", 0x00FF0000, max_slots);
        
        // Enable all supported slots
        xhci_op_regs->config = max_slots;
        kprintf("xHCI: Set CONFIG register to enable %u slots\n", 0x00FF0000, max_slots);

        // --- Scratchpad Buffer Allocation (Required for real hardware) ---
        // Read HCSPARAMS2 (Offset 0x08)
        uint32_t hcsparams2 = xhci_cap_regs->hcsparams2;
        // Max Scratchpad Buffers = (Hi << 5) | Lo
        // Hi = Bits 25:21 (Wait, spec says Hi is 31:27? Let's verify spec)
        // Spec 5.3.4: 
        // Bits 31:27 = Max Scratchpad Buffers Hi
        // Bits 25:21 = Max Scratchpad Buffers Lo
        // Let's re-check typical offsets. Actually bits 25:21 is usually MaxScratchpadBufsLo in HCSPARAMS2
        // Bit 26 is SPR (Scratchpad Restore).
        
        uint32_t max_scratchpad_bufs_lo = (hcsparams2 >> 21) & 0x1F;
        uint32_t max_scratchpad_bufs_hi = (hcsparams2 >> 27) & 0x1F;
        uint32_t max_scratchpad_bufs = (max_scratchpad_bufs_hi << 5) | max_scratchpad_bufs_lo;
        
        kprintf("xHCI: Max Scratchpad Buffers required: %u\n", 0x00FF0000, max_scratchpad_bufs);
        
        if (max_scratchpad_bufs > 0) {
            // Limit to reasonable number to avoid excessive allocation
            if (max_scratchpad_bufs > 256) {
                kprintf("xHCI: Warning: Limiting scratchpad bufs from %u to 256\n", 0xFFFF00, max_scratchpad_bufs);
                max_scratchpad_bufs = 256;
            }
            
            // Allocate Scratchpad Buffer Array (must be 64-byte aligned, pfa_alloc gives 4KB alignment)
            // Use pfa_alloc_low to get memory below 4GB that's identity mapped
            kprintf("xHCI: Allocating scratchpad array page...\n", 0x00FF0000);
            uint64_t scratchpad_array_phys = pfa_alloc_low();
            if (scratchpad_array_phys == 0) {
                kprintf("xHCI: Failed to allocate scratchpad array!\n", 0xFF0000);
                xhci_dcbaap_array[0] = 0;
            } else {
                kprintf("xHCI: Scratchpad array phys=0x%lx\n", 0x00FF0000, scratchpad_array_phys);
                uint64_t *scratchpad_array_virt = (uint64_t*)phys_to_virt(scratchpad_array_phys);
                kprintf("xHCI: Scratchpad array virt=0x%lx\n", 0x00FF0000, (uint64_t)scratchpad_array_virt);
                
                // Clear the array
                custom_memset(scratchpad_array_virt, 0, 4096); 
                
                kprintf("xHCI: Allocating %u scratchpad pages...\n", 0x00FF0000, max_scratchpad_bufs);
                for (uint32_t i = 0; i < max_scratchpad_bufs; i++) {
                    // Scratchpad pages can be any memory - xHCI accesses them via DMA
                    // But their addresses go into scratchpad_array_virt which we write via CPU
                    uint64_t scratchpad_page_phys = pfa_alloc();  // Can be any memory for DMA
                    if (scratchpad_page_phys == 0) {
                        kprintf("xHCI: Failed to allocate scratchpad page %u\n", 0xFF0000, i);
                        break;
                    }
                    // Don't clear pages - too slow and not required
                    scratchpad_array_virt[i] = scratchpad_page_phys;
                }
                kprintf("xHCI: Scratchpad pages allocated.\n", 0x00FF0000);
                
                // Point DCBAA[0] to the Scratchpad Buffer Array
                xhci_dcbaap_array[0] = scratchpad_array_phys;
                kprintf("xHCI: DCBAAP[0] = 0x%lx\n", 0x00FF0000, xhci_dcbaap_array[0]);
            }
        } else {
             xhci_dcbaap_array[0] = 0;
        }

        // Set the DCBAA Address Register
        uint64_t dcbaap_phys = virt_to_phys(xhci_dcbaap_array);
        if (dcbaap_phys >= 0x100000000ULL) {
            kprintf("xHCI: WARNING! DCBAAP at 0x%lx (>4GB) - DMA may fail!\n", 0xFF0000, dcbaap_phys);
        }
        xhci_op_regs->dcbaap = dcbaap_phys;
        kprintf("xHCI: DCBAAP physical address set to 0x%lx\n", 0x00FF0000, xhci_op_regs->dcbaap);

        // Initialize Command Ring State
        cmd_ring_enqueue_ptr = 0;
        cmd_ring_cycle_state = 1; // Consumer Cycle State (CCS) starts at 1
        
        // Initialize Event Ring State
        event_ring_dequeue_ptr = 0;
        event_ring_cycle_state = 1; // Producer Cycle State (PCS) starts at 1

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

        // First pass: Power on ALL ports (some real hardware needs this)
        kprintf("xHCI: Powering on all ports...\n", 0x00FF0000);
        for (uint32_t i = 0; i < max_ports; i++) {
            volatile uint32_t *portsc_reg = (uint32_t*)((uint8_t*)xhci_op_regs + 0x400 + (i * 0x10));
            uint32_t portsc_val = *portsc_reg;
            
            // Set Port Power (PP - bit 9) if not already set
            if (!(portsc_val & (1 << 9))) {
                *portsc_reg = portsc_val | (1 << 9);
            }
        }
        
        // Wait for ports to stabilize (real hardware needs 100ms+ per USB spec)
        kprintf("xHCI: Waiting for port stabilization...\n", 0x00FF0000);
        for (volatile int delay = 0; delay < 5000000; delay++);  // ~100-500ms delay

        // Second pass: Scan for connected devices
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
                
                // Check controller state first
                uint32_t usbsts_before = xhci_op_regs->usbsts;
                uint32_t usbcmd_before = xhci_op_regs->usbcmd;
                kprintf("xHCI: Pre-cmd state: USBCMD=0x%x USBSTS=0x%x\n", 0x00FF0000, usbcmd_before, usbsts_before);
                
                // Clear EINT (Event Interrupt) and PCD bits by writing 1 to them
                if (usbsts_before & ((1 << 3) | (1 << 4))) {
                    kprintf("xHCI: Clearing pending status bits...\n", 0xFFFF00);
                    xhci_op_regs->usbsts = (1 << 3) | (1 << 4);  // Write 1s to clear R/WC bits
                }
                
                // Process any pending events in the event ring first
                kprintf("xHCI: Flushing event ring...\n", 0x00FF0000);
                for (int flush = 0; flush < 16; flush++) {
                    xhci_trb_t *event_trb = &xhci_event_ring[event_ring_dequeue_ptr];
                    uint32_t event_cycle = (event_trb->control & 1) ? 1 : 0;
                    if (event_cycle != event_ring_cycle_state) break;
                    
                    uint32_t trb_type = (event_trb->control >> 10) & 0x3F;
                    kprintf("xHCI: Flushed event type %u\n", 0x00FF0000, trb_type);
                    
                    event_ring_dequeue_ptr = (event_ring_dequeue_ptr + 1) % XHCI_EVENT_RING_SIZE;
                    if (event_ring_dequeue_ptr == 0) {
                        event_ring_cycle_state = !event_ring_cycle_state;
                    }
                }
                // Update ERDP
                volatile uint64_t *erdp_flush = (uint64_t*)((uint8_t*)xhci_runtime_regs + 0x38);
                *erdp_flush = virt_to_phys(&xhci_event_ring[event_ring_dequeue_ptr]) | (1 << 3);
                
                if (usbsts_before & (1 << 0)) {  // HCH bit - Controller Halted
                    kprintf("xHCI: ERROR - Controller is halted!\n", 0xFF0000);
                    continue;
                }
                
                xhci_trb_t enable_slot_cmd;
                custom_memset(&enable_slot_cmd, 0, sizeof(xhci_trb_t));
                // TRB Type (bits 10-15) = 9 (Enable Slot)
                // Slot Type (bits 16-19) = 0 (Device Slot, default for USB 2.0/3.0)
                enable_slot_cmd.control = (TRB_TYPE_ENABLE_SLOT << 10);
                kprintf("xHCI: Enable Slot TRB control=0x%x for Port %u\n", 0x00FF0000, enable_slot_cmd.control, port_id);
                
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
    // xHCI Speed IDs: 1=Full(12M), 2=Low(1.5M), 3=High(480M), 4=Super(5G), 5=Super+(10G)
    // USB 1.1 Low Speed max packet = 8, Full Speed = 64, High Speed = 64
    kprintf("xHCI: Port speed ID = %u\n", 0x00FF0000, port_speed);
    uint16_t max_packet_size = 0;
    switch (port_speed) {
        case 1: max_packet_size = 64; break; // Full-speed (12 Mbps)
        case 2: max_packet_size = 8; break;  // Low-speed (1.5 Mbps) 
        case 3: max_packet_size = 64; break; // High-speed (480 Mbps)
        case 4: max_packet_size = 512; break;// SuperSpeed (5 Gbps)
        case 5: max_packet_size = 1024; break;// SuperSpeed+ (10 Gbps)
        default: max_packet_size = 64; break; // Default
    }
    kprintf("xHCI: Using max_packet_size = %u for speed %u\n", 0x00FF0000, max_packet_size, port_speed);
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
        // Store this slot as a keyboard (assume first USB device is keyboard)
        usb_kbd_slot = slot_id;
        usb_kbd_max_packet = max_packet_size;
        usb_kbd_port_speed = port_speed;
        usb_kbd_port_id = port_id;
        // Add a placeholder nvnode for this device.
        nvnode_add_usb_device(0, 0);
        kprintf("xHCI: USB Keyboard registered on Slot %u\n", 0x00FFFF00, slot_id);
        
        // Configure EP1 (interrupt IN) for HID keyboard
        xhci_configure_kbd_endpoint(slot_id, port_speed);
    } else {
        kprintf("xHCI: Address Device failed for Slot %u. Code: %u\n", 0xFF0000, slot_id, completion_code);
    }
}

// Configure keyboard interrupt endpoint (EP1 IN)
static void xhci_configure_kbd_endpoint(uint32_t slot_id, uint32_t port_speed) {
    kprintf("xHCI: Configuring EP1 (interrupt IN) for keyboard...\n", 0x00FF0000);
    
    xhci_input_context_t *input_ctx = &xhci_input_context_pool[slot_id];
    custom_memset(input_ctx, 0, sizeof(xhci_input_context_t));
    
    // Initialize EP1 transfer ring
    custom_memset(usb_kbd_ep1_ring, 0, sizeof(usb_kbd_ep1_ring));
    usb_kbd_ep1_enqueue = 0;
    usb_kbd_ep1_cycle = 1;
    
    // Input Control Context: A0=1 (Slot), A2=1 (EP1 IN = DCI 3)
    // EP1 IN = endpoint address 0x81, DCI = (1 * 2) + 1 = 3
    input_ctx->add_flags = (1 << 0) | (1 << 3);  // Slot + EP1 IN (DCI 3)
    
    // Copy current slot context info
    xhci_device_context_t *dev_ctx = &xhci_device_context_pool[slot_id];
    input_ctx->slot.dwords[0] = dev_ctx->slot.dwords[0];
    input_ctx->slot.dwords[1] = dev_ctx->slot.dwords[1];
    // Update Context Entries to include EP1 (value = 3 for DCI 3)
    input_ctx->slot.dwords[0] = (input_ctx->slot.dwords[0] & ~(0x1F << 27)) | (3 << 27);
    
    // Configure EP1 IN (index 2 in eps[] array, since eps[0] is EP0 OUT, eps[1] is EP0 IN)
    // Actually, for EP1 IN, the index in eps[] is (DCI - 1) = 2
    xhci_endpoint_context_t *ep1_ctx = &input_ctx->eps[2];
    
    // EP Type: Interrupt IN = 7, Max Packet Size = 8 for keyboards
    // Interval depends on speed: Low/Full = 10ms, High = 4ms
    uint32_t interval = (port_speed >= 3) ? 4 : 10;
    ep1_ctx->dwords[0] = (interval << 16);  // Interval
    ep1_ctx->dwords[1] = (7 << 3) | (8 << 16) | (3 << 1);  // EP Type=7 (Int IN), MaxPacketSize=8, CErr=3
    
    // TR Dequeue Pointer (physical address of EP1 ring | DCS)
    uint64_t ep1_ring_phys = virt_to_phys(usb_kbd_ep1_ring) | 1;
    *((uint64_t*)&ep1_ctx->dwords[2]) = ep1_ring_phys;
    
    // Average TRB Length
    ep1_ctx->dwords[4] = 8;
    
    // Send Configure Endpoint command
    xhci_trb_t config_cmd;
    custom_memset(&config_cmd, 0, sizeof(xhci_trb_t));
    config_cmd.parameter = virt_to_phys(input_ctx);
    config_cmd.control = (slot_id << 24) | (TRB_TYPE_CONFIGURE_ENDPOINT << 10);
    
    uint32_t completion_code;
    if (xhci_send_command(&config_cmd, &completion_code, NULL) == 0) {
        kprintf("xHCI: Configure Endpoint successful. EP1 ready for keyboard.\n", 0x00FF00);
        usb_kbd_ep1_configured = 1;
        
        // Send SET_PROTOCOL to enable Boot Protocol
        xhci_set_boot_protocol(slot_id);
        
        // Queue initial transfer TRB to receive keyboard reports
        xhci_queue_kbd_transfer();
    } else {
        kprintf("xHCI: Configure Endpoint failed. Code: %u\n", 0xFF0000, completion_code);
    }
}

// Static buffer for control transfer data
static __attribute__((aligned(64))) uint8_t usb_ctrl_data[64];
static __attribute__((aligned(64))) xhci_trb_t usb_ep0_ring[8];
static uint32_t usb_ep0_enqueue = 0;
static uint32_t usb_ep0_cycle = 1;

// Send SET_PROTOCOL (Boot Protocol) to HID keyboard
static void xhci_set_boot_protocol(uint32_t slot_id) {
    kprintf("xHCI: Sending SET_PROTOCOL (Boot Protocol)...\n", 0x00FF0000);
    
    // Setup Stage TRB for SET_PROTOCOL
    // bmRequestType = 0x21 (Class, Interface, Host to Device)
    // bRequest = 0x0B (SET_PROTOCOL)
    // wValue = 0x0000 (Boot Protocol)
    // wIndex = 0x0000 (Interface 0)
    // wLength = 0
    
    xhci_trb_t *trb = &xhci_ep_transfer_rings[slot_id][0];
    custom_memset(trb, 0, sizeof(xhci_trb_t) * 3);
    
    // Setup Stage TRB
    // Parameter: bmRequestType(8) | bRequest(8) | wValue(16) | wIndex(16) | wLength(16) 
    uint64_t setup_data = 0x21 | (0x0B << 8) | (0x0000 << 16) | ((uint64_t)0x0000 << 32) | ((uint64_t)0x0000 << 48);
    trb[0].parameter = setup_data;
    trb[0].status = 8;  // TRB Transfer Length = 8 (setup packet size)
    trb[0].control = (TRB_TYPE_SETUP_STAGE << 10) | (1 << 6) | 1;  // IDT=1 (Immediate Data), Cycle=1, TRT=0 (No data)
    
    // Status Stage TRB (direction = IN for no-data control)
    trb[1].parameter = 0;
    trb[1].status = 0;
    trb[1].control = (TRB_TYPE_STATUS_STAGE << 10) | (1 << 5) | (1 << 16) | 1;  // IOC=1, DIR=1 (IN), Cycle=1
    
    // Ring doorbell for EP0 (DCI 1)
    xhci_doorbell_regs[slot_id] = 1;
    
    // Wait briefly for completion
    for (volatile int i = 0; i < 100000; i++);
    
    kprintf("xHCI: SET_PROTOCOL sent.\n", 0x00FF00);
}

// Queue a transfer TRB to receive keyboard input
static void xhci_queue_kbd_transfer(void) {
    if (!usb_kbd_ep1_configured || !usb_kbd_slot) return;
    
    // Reserve last entry for Link TRB
    int usable_slots = XHCI_EP_RING_SIZE - 1;
    
    // Create Normal TRB for receiving HID report
    xhci_trb_t *trb = &usb_kbd_ep1_ring[usb_kbd_ep1_enqueue];
    custom_memset(trb, 0, sizeof(xhci_trb_t));
    
    trb->parameter = virt_to_phys(usb_kbd_report_data);
    trb->status = 8;  // Transfer Length = 8 bytes
    trb->control = (TRB_TYPE_NORMAL << 10) | (1 << 5);  // IOC (Interrupt On Complete)
    
    // Set cycle bit
    if (usb_kbd_ep1_cycle) {
        trb->control |= 1;
    }
    
    // Advance enqueue pointer
    usb_kbd_ep1_enqueue++;
    
    // If we hit the last slot, add Link TRB and wrap around
    if (usb_kbd_ep1_enqueue >= usable_slots) {
        xhci_trb_t *link = &usb_kbd_ep1_ring[usable_slots];
        custom_memset(link, 0, sizeof(xhci_trb_t));
        link->parameter = virt_to_phys(usb_kbd_ep1_ring);  // Point back to start
        link->control = (TRB_TYPE_LINK << 10) | (1 << 1);  // Toggle Cycle bit
        if (usb_kbd_ep1_cycle) link->control |= 1;
        
        usb_kbd_ep1_enqueue = 0;
        usb_kbd_ep1_cycle = !usb_kbd_ep1_cycle;
    }
    
    // Ring the doorbell for EP1 IN (DCI 3)
    xhci_doorbell_regs[usb_kbd_slot] = 3;
}

// External USB HID processing function
// External USB HID processing function
extern void usb_kbd_process_report(void *report);

// Poll for USB keyboard events (called from input wait loop)
void usb_kbd_poll(void) {
    if (!usb_kbd_ep1_configured || !xhci_op_regs) return;
    
    // Check multiple events in case they accumulated
    for (int i = 0; i < 4; i++) {
        xhci_trb_t *event_trb = &xhci_event_ring[event_ring_dequeue_ptr];
        uint32_t event_cycle = (event_trb->control & 1) ? 1 : 0;
        
        if (event_cycle != event_ring_cycle_state) {
            // No more events
            break;
        }
        
        uint32_t trb_type = (event_trb->control >> 10) & 0x3F;
        uint32_t completion_code = (event_trb->status >> 24) & 0xFF;
        uint32_t slot = (event_trb->control >> 24) & 0xFF;
        
        // Advance event ring dequeue pointer
        event_ring_dequeue_ptr = (event_ring_dequeue_ptr + 1) % XHCI_EVENT_RING_SIZE;
        if (event_ring_dequeue_ptr == 0) {
            event_ring_cycle_state = !event_ring_cycle_state;
        }
        
        // Update ERDP
        volatile uint64_t *erdp = (uint64_t*)((uint8_t*)xhci_runtime_regs + 0x38);
        *erdp = virt_to_phys(&xhci_event_ring[event_ring_dequeue_ptr]) | (1 << 3);
        
        if (trb_type == TRB_TYPE_TRANSFER_EVENT) {
            if (slot == usb_kbd_slot && completion_code == 1) {
                // Success! Process the HID report
                usb_kbd_process_report(usb_kbd_report_data);
                
                // Queue next transfer
                xhci_queue_kbd_transfer();
            } else if (completion_code != 1) {
                // Transfer failed, but still requeue
                xhci_queue_kbd_transfer();
            }
        }
        // Ignore other event types (port status change etc)
    }
}