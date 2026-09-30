#include <stdint.h>

#include "interrupts.h"
#include "minemu/irq.h"
#include "minemu/platform.h"
#include "uart.h"

/*
 * Receive buffer shared between the UART IRQ handler (writer) and the
 * main kernel code (reader). It is a circular buffer: the handler adds
 * bytes at rx_head and uart_getc() removes them from rx_tail.
 *
 * rx_head and rx_tail only ever count up; the slot used is the count
 * modulo the buffer size. So head == tail means empty, and
 * head - tail == RX_BUF_SIZE means full (unsigned wraparound keeps the
 * subtraction correct).
 *
 * The size matches the UART's own receive queue, so a full hardware
 * queue drained in one interrupt still fits.
 */
#define RX_BUF_SIZE MINEMU_UART_RX_CAPACITY

static volatile char rx_buf[RX_BUF_SIZE];
static volatile unsigned int rx_head;
static volatile unsigned int rx_tail;

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

void uart_init(void) {
    rx_head = 0;
    rx_tail = 0;
    MINEMU_UART0->control = MINEMU_UART_CONTROL_RX_IRQ_ENABLE;
    irq_enable_source(MINEMU_IRQ_UART0);
}

void uart_irq_handler(void) {
    /* Runs in IRQ mode with IRQs already masked by the hardware.
     * Read every available byte: the UART keeps its interrupt asserted
     * while RX data remains, so stopping early would re-trigger it. */
    while ((MINEMU_UART0->status & MINEMU_UART_STATUS_RX_READY) != 0) {
        char c = (char)(MINEMU_UART0->rx_data & 0xff);
        if (rx_head - rx_tail < RX_BUF_SIZE) {
            rx_buf[rx_head % RX_BUF_SIZE] = c;
            rx_head++;
        }
        /* If the buffer is full the byte is dropped, but it was still
         * read so the hardware queue keeps draining. */
    }
}

int uart_getc(void) {
    int c = -1;

    /* The IRQ handler also changes the buffer, so keep it from running
     * while we check and update the shared indices. */
    minemu_irq_disable();
    if (rx_tail != rx_head) {
        c = (unsigned char)rx_buf[rx_tail % RX_BUF_SIZE];
        rx_tail++;
    }
    minemu_irq_enable();

    return c;
}
