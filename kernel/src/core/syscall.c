#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "minemu/mmu.h"
#include "minemu/platform.h"
#include "minemu/syscall.h"
#include "minemu/trace.h"
#include "minemu/trap.h"
#include "minemu/user_abi.h"
#include "uart.h"
#include "user.h"

#define CPSR_MODE_MASK UINT32_C(0x1f)
#define CPSR_MODE_USR UINT32_C(0x10)
#define CPSR_THUMB UINT32_C(0x20)
#define A32_SVC_ZERO UINT32_C(0xef000000) /* encoding of "svc #0" */

static int32_t uart_ioctl(volatile struct minemu_uart_regs *port, uint32_t op, uint32_t buf,
                          uint32_t n_arg) {
    /* UART data goes through this kernel buffer, so user memory is only
     * touched by copy_from_user / copy_to_user. */
    char kbuf[MINEMU_IOCTL_MAX_TRANSFER];
    int32_t n = (int32_t)n_arg;

    if (op == MINEMU_IOCTL_UART_STATUS) {
        int32_t status = 0;
        if (uart_port_rx_ready(port)) {
            status |= MINEMU_IOCTL_UART_RX_READY;
        }
        if ((port->status & MINEMU_UART_STATUS_TX_READY) != 0) {
            status |= MINEMU_IOCTL_UART_TX_READY;
        }
        return status;
    }

    bool is_read = (op == MINEMU_IOCTL_UART_READ);
    bool is_write = (op == MINEMU_IOCTL_UART_WRITE);
    if ((!is_read && !is_write) || n < 0) {
        return MINEMU_IOCTL_ERROR;
    }

    uint32_t total = (uint32_t)n;
    uint32_t done = 0;

    if (is_write) {
        /* Check the whole source range before sending anything, then
         * copy it over at most MINEMU_IOCTL_MAX_TRANSFER bytes at a time. */
        if (!user_range_ok(buf, total, MINEMU_PTE_READABLE)) {
            return MINEMU_IOCTL_ERROR;
        }
        while (done < total) {
            uint32_t chunk = total - done;
            if (chunk > MINEMU_IOCTL_MAX_TRANSFER) {
                chunk = MINEMU_IOCTL_MAX_TRANSFER;
            }
            if (!copy_from_user(kbuf, buf + done, chunk)) {
                return MINEMU_IOCTL_ERROR;
            }
            uart_port_write(port, kbuf, chunk);
            done += chunk;
        }
        return n;
    }

    /* Read: check the destination before taking any input, so a bad
     * pointer can't make us throw away received bytes. */
    if (!user_range_ok(buf, total, MINEMU_PTE_WRITABLE)) {
        return MINEMU_IOCTL_ERROR;
    }
    while (done < total) {
        uint32_t want = total - done;
        if (want > MINEMU_IOCTL_MAX_TRANSFER) {
            want = MINEMU_IOCTL_MAX_TRANSFER;
        }
        size_t got = uart_port_read(port, kbuf, want);
        if (!copy_to_user(buf + done, kbuf, got)) {
            return MINEMU_IOCTL_ERROR;
        }
        done += (uint32_t)got;
        if (got < want) {
            break; /* nothing else has arrived yet */
        }
    }
    return (int32_t)done;
}

static int32_t rng_ioctl(uint32_t op, uint32_t arg) {
    if (op == MINEMU_IOCTL_RNG_SEED) {
        MINEMU_RNG->seed = arg;
        return 0;
    }

    bool next = (op == MINEMU_IOCTL_RNG_NEXT);
    if (!next && op != MINEMU_IOCTL_RNG_STATE) {
        return MINEMU_IOCTL_ERROR;
    }
    /* Check the output pointer first: reading DATA advances the RNG,
     * which shouldn't happen if we can't return the value. */
    uint32_t value;
    if (!user_range_ok(arg, sizeof(value), MINEMU_PTE_WRITABLE)) {
        return MINEMU_IOCTL_ERROR;
    }
    value = next ? MINEMU_RNG->data : MINEMU_RNG->state;
    if (!copy_to_user(arg, &value, sizeof(value))) {
        return MINEMU_IOCTL_ERROR;
    }
    return 0;
}

static int32_t trace_ioctl(uint32_t op, uint32_t value) {
    if (op != MINEMU_IOCTL_TRACE_EVENT) {
        return MINEMU_IOCTL_ERROR;
    }
    minemu_trace_event(value);
    return 0;
}

/* ioctl(fd, op, arg2, arg3): pick the device from fd. */
static int32_t sys_ioctl(uint32_t fd, uint32_t op, uint32_t arg2, uint32_t arg3) {
    switch (fd) {
    case MINEMU_FD_UART_IN:
    case MINEMU_FD_UART_OUT:
        return uart_ioctl(MINEMU_UART0, op, arg2, arg3);
    case MINEMU_FD_UART_DIAG:
        return uart_ioctl(MINEMU_UART1, op, arg2, arg3);
    case MINEMU_FD_RNG:
        return rng_ioctl(op, arg2);
    case MINEMU_FD_TRACE:
        return trace_ioctl(op, arg2);
    default:
        return MINEMU_IOCTL_ERROR;
    }
}

/* Only accept an A32 "svc #0" that came from user mode. */
static bool svc_from_user(const struct minemu_trap_frame *frame) {
    if (frame->exception_id != MINEMU_EXCEPTION_SVC) {
        return false;
    }
    if ((frame->spsr & CPSR_MODE_MASK) != CPSR_MODE_USR || (frame->spsr & CPSR_THUMB) != 0) {
        return false;
    }
    uint32_t instruction;
    if (!copy_from_user(&instruction, frame->fault_pc, sizeof(instruction))) {
        return false;
    }
    return instruction == A32_SVC_ZERO;
}

struct minemu_trap_frame *minemu_svc_dispatch(struct minemu_trap_frame *frame) {
    int32_t result;

    if (!svc_from_user(frame)) {
        result = MINEMU_IOCTL_ERROR;
    } else {
        /* Syscall number is in r7, arguments in r0-r3. */
        switch (frame->r[7]) {
        case MINEMU_SYSCALL_IOCTL:
            result = sys_ioctl(frame->r[0], frame->r[1], frame->r[2], frame->r[3]);
            break;
        default:
            result = MINEMU_IOCTL_ERROR;
            break;
        }
    }

    /* The trampoline restores r0 from the frame, so this is the value the
     * user program gets back. */
    frame->r[0] = (uint32_t)result;
    return frame;
}
