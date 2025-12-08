#include "irq.h"
#include "ps2.h"

void irq_handler(int irq) {
    switch (irq) {
        case 0:
            // timer: not used yet
            break;
        case 1:
            // keyboard
            ps2_handle_interrupt();
            break;
        default:
            break;
    }
}
