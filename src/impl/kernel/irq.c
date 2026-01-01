#include "irq.h"
#include "ps2.h"

// Timer handler (defined in timer.c)
extern void timer_irq_handler(void);

void irq_handler(int irq) {
    switch (irq) {
        case 0:
            // Timer interrupt
            timer_irq_handler();
            break;
        case 1:
            // Keyboard interrupt
            ps2_handle_interrupt();
            break;
        default:
            break;
    }
}
