#ifndef UART_H
#define UART_H

#include <stddef.h>

/*
 * UART0 console driver.
 *
 * Transmit is polled: each byte waits for TX ready in the status
 * register before it is written to tx_data.
 *
 * Receive is interrupt-driven: the IRQ handler drains rx_data into a
 * software buffer, and uart_getc() takes bytes out of that buffer.
 */
void uart_putc(char c);
void uart_puts(const char *s);
void uart_write(const char *buf, size_t len);

/* Enable RX interrupts for UART0 (device and interrupt controller).
 * CPU IRQs still need to be unmasked by the caller. */
void uart_init(void);

/* Called from minemu_irq_dispatch when UART0 interrupts. */
void uart_irq_handler(void);

/* Return the next received byte, or -1 if none is buffered.
 * Must only be called with IRQs enabled (not from an IRQ handler). */
int uart_getc(void);

#endif
