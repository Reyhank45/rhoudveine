#include <stdint.h>
#include "idt.h"

extern void isr_default_handler();
extern void isr_irq0();
extern void isr_irq1();

// IDT entry structure (packed)
struct __attribute__((packed)) idt_entry {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_hi;
    uint32_t zero;
};

struct __attribute__((packed)) idtr {
    uint16_t limit;
    uint64_t base;
};

static struct idt_entry idt[256];

static void set_idt_entry(int n, void (*handler)()) {
    uint64_t addr = (uint64_t)handler;
    idt[n].offset_lo = addr & 0xFFFF;
    idt[n].selector = 0x08; // code segment
    idt[n].ist = 0;
    idt[n].type_attr = 0x8E; // interrupt gate, present
    idt[n].offset_mid = (addr >> 16) & 0xFFFF;
    idt[n].offset_hi = (addr >> 32) & 0xFFFFFFFF;
    idt[n].zero = 0;
}

static inline void lidt(void* ptr) {
    struct idtr idtr;
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint64_t)&idt;
    __asm__ volatile ("lidt %0" : : "m" (idtr));
}

// PIC ports
#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xA0
#define PIC2_DATA 0xA1

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a" (val), "Nd" (port));
}

static void pic_remap(void) {
    // ICW1 - start initialization
    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);
    // ICW2 - remap offset
    outb(PIC1_DATA, 0x20); // Master PIC vector offset
    outb(PIC2_DATA, 0x28); // Slave PIC vector offset
    // ICW3 - tell Master about Slave at IRQ2 (0000 0100)
    outb(PIC1_DATA, 0x04);
    // ICW3 - tell Slave its cascade identity (0000 0010)
    outb(PIC2_DATA, 0x02);
    // ICW4 - 8086/88 (MCS-80/85) mode
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
    // Mask all IRQs initially, then unmask keyboard (IRQ1)
    // PIC1 mask: bit cleared = enabled. 0xFF = all masked.
    outb(PIC1_DATA, 0xFF & ~(1 << 1)); // enable IRQ1 (keyboard)
    outb(PIC2_DATA, 0xFF);
}

void init_idt(void) {
    for (int i = 0; i < 256; i++) set_idt_entry(i, isr_default_handler);
    /* Set IRQ0 and IRQ1 to our stubs (vectors 32 and 33) */
    set_idt_entry(32 + 0, isr_irq0);
    set_idt_entry(32 + 1, isr_irq1);
    pic_remap();
    lidt(idt);
}
