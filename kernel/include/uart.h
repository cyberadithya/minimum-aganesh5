#ifndef UART_H
#define UART_H

#include <stddef.h>

/*
 * UART0 console driver.
 *
 * Transmit is polled: each byte waits for TX ready in the status
 * register before it is written to tx_data.
 */
void uart_putc(char c);
void uart_puts(const char *s);
void uart_write(const char *buf, size_t len);

#endif
