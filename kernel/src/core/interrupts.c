#include <stddef.h>
#include <stdint.h>

#include "interrupts.h"
#include "minemu/irq.h"
#include "minemu/platform.h"
#include "minemu/runtime.h"
#include "uart.h"

#define IRQ_SOURCE_COUNT 4

typedef void (*irq_handler_t)(void);

/* Handler for each interrupt source ID. NULL means no driver yet. */
static const irq_handler_t irq_handlers[IRQ_SOURCE_COUNT] = {
    [MINEMU_IRQ_SYSTICK] = NULL,
    [MINEMU_IRQ_UART0] = uart_irq_handler,
    [MINEMU_IRQ_UART1] = NULL,
    [MINEMU_IRQ_BLOCK] = NULL,
};

void irq_enable_source(uint32_t source) {
    MINEMU_INTERRUPT->enable = MINEMU_INTERRUPT->enable | (UINT32_C(1) << source);
}

struct minemu_trap_frame *minemu_irq_dispatch(struct minemu_trap_frame *frame) {
    uint32_t source = (uint32_t)frame->exception_id;

    /* CLAIM returns MINEMU_IRQ_NONE if nothing was claimable. Writing EOI
     * without an active claim is an invalid access, so just return. */
    if (source >= IRQ_SOURCE_COUNT) {
        return frame;
    }

    /* A source with no handler would never have its level cleared and
     * would interrupt forever, so stop loudly instead. */
    if (irq_handlers[source] == NULL) {
        minemu_panic("unhandled IRQ source");
    }

    irq_handlers[source]();

    /* The handler has cleared the device's reason for interrupting,
     * so now complete the claim in the controller. */
    MINEMU_INTERRUPT->eoi = source;

    /* Same frame for now; scheduling may return a different one later. */
    return frame;
}
