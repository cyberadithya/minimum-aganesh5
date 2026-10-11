#include <stdbool.h>
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

static void port_putc(volatile struct minemu_uart_regs *port, char c) {
    /* Wait until the UART can accept another byte. */
    while ((port->status & MINEMU_UART_STATUS_TX_READY) == 0) {
    }
    /* Cast through uint8_t so only the low 8 bits are ever written;
     * setting unsupported bits in tx_data causes a data abort. */
    port->tx_data = (uint32_t)(uint8_t)c;
}

void uart_putc(char c) {
    port_putc(MINEMU_UART0, c);
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

/* Move every byte waiting in UART0's receive queue into rx_buf.
 * Must run with IRQs masked. */
static void rx_drain(void) {
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

void uart_irq_handler(void) {
    /* Runs in IRQ mode with IRQs already masked by the hardware.
     * Read every available byte: the UART keeps its interrupt asserted
     * while RX data remains, so stopping early would re-trigger it. */
    rx_drain();
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

void uart_port_write(volatile struct minemu_uart_regs *port, const char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        port_putc(port, buf[i]);
    }
}

size_t uart_port_read(volatile struct minemu_uart_regs *port, char *dst, size_t max) {
    size_t count = 0;

    if (port == MINEMU_UART0) {
        /* UART0 input is normally taken by the IRQ handler, so read from
         * the software buffer. IRQs are already masked here (syscall), so
         * the handler can't change the indices while we use them.
         * Drain the device first too: a user program that polls with
         * back-to-back syscalls spends most of its time in the kernel with
         * IRQs masked, so the UART interrupt may not have run yet. */
        rx_drain();
        while (count < max && rx_tail != rx_head) {
            dst[count] = rx_buf[rx_tail % RX_BUF_SIZE];
            rx_tail++;
            count++;
        }
    } else {
        /* UART1 has no RX interrupt enabled, so its bytes are still in
         * the device's receive queue. Read them directly. */
        while (count < max && (port->status & MINEMU_UART_STATUS_RX_READY) != 0) {
            dst[count] = (char)(port->rx_data & 0xff);
            count++;
        }
    }
    return count;
}

bool uart_port_rx_ready(volatile struct minemu_uart_regs *port) {
    if (port == MINEMU_UART0) {
        /* UART0 input lives in the software buffer (see above). */
        rx_drain();
        return rx_tail != rx_head;
    }
    return (port->status & MINEMU_UART_STATUS_RX_READY) != 0;
}
