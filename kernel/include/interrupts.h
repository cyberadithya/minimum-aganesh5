#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

/* Enable delivery of one peripheral's interrupts in the interrupt
 * controller (source IDs are MINEMU_IRQ_* in minemu/platform.h). */
void irq_enable_source(uint32_t source);

#endif
