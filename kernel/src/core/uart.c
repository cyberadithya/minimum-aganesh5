#include <stdint.h>

#include "minemu/platform.h"
#include "uart.h"

void uart_putc(char c) {
    /* Wait until UART0 can accept another byte. */
    while ((MINEMU_UART0->status & MINEMU_UART_STATUS_TX_READY) == 0) {
    }
    /* Cast through uint8_t so only the low 8 bits are ever written;
     * setting unsupported bits in tx_data causes a data abort. */
    MINEMU_UART0->tx_data = (uint32_t)(uint8_t)c;
}

void uart_write(const char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uart_putc(buf[i]);
    }
}

void uart_puts(const char *s) {
    while (*s != '\0') {
        uart_putc(*s);
        s++;
    }
}
