#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "kprintf.h"
#include "uart.h"

/*
 * These avoid '/' and '%' on purpose. On this target the compiler turns
 * division into a call to a libgcc helper, and the libgcc the toolchain
 * links is built as Thumb code, which minemu cannot execute (Platform
 * ABI: Thumb execution is unsupported). So hex uses shifts, and decimal
 * uses repeated subtraction of powers of ten.
 */
static void print_hex(unsigned int value) {
    const char digits[] = "0123456789abcdef";
    int started = 0;

    /* Take 4 bits at a time, most significant first, skipping leading
     * zeros (but always print the last digit so 0 prints as "0"). */
    for (int shift = 28; shift >= 0; shift -= 4) {
        unsigned int d = (value >> shift) & 0xf;
        if (d != 0 || started || shift == 0) {
            uart_putc(digits[d]);
            started = 1;
        }
    }
}

static void print_decimal(unsigned int value) {
    static const unsigned int powers[] = {
        1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
        10000u, 1000u, 100u, 10u, 1u,
    };
    int started = 0;

    for (size_t i = 0; i < sizeof(powers) / sizeof(powers[0]); i++) {
        /* Count how many times this power of ten fits: that's the digit. */
        unsigned int d = 0;
        while (value >= powers[i]) {
            value -= powers[i];
            d++;
        }
        if (d != 0 || started || powers[i] == 1u) {
            uart_putc((char)('0' + d));
            started = 1;
        }
    }
}

static void print_signed(int value) {
    if (value < 0) {
        uart_putc('-');
        /* Negate as unsigned so the most negative int still works. */
        print_decimal(0u - (unsigned int)value);
    } else {
        print_decimal((unsigned int)value);
    }
}

void kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    for (const char *p = fmt; *p != '\0'; p++) {
        if (*p != '%') {
            uart_putc(*p);
            continue;
        }

        p++;
        if (*p == '\0') {
            break; /* lone '%' at the end of the string */
        }

        switch (*p) {
        case 'c':
            uart_putc((char)va_arg(args, int));
            break;
        case 's': {
            const char *s = va_arg(args, const char *);
            uart_puts(s != NULL ? s : "(null)");
            break;
        }
        case 'd':
            print_signed(va_arg(args, int));
            break;
        case 'u':
            print_decimal(va_arg(args, unsigned int));
            break;
        case 'x':
            print_hex(va_arg(args, unsigned int));
            break;
        case 'p':
            uart_puts("0x");
            print_hex((unsigned int)(uintptr_t)va_arg(args, void *));
            break;
        case '%':
            uart_putc('%');
            break;
        default:
            /* Unknown conversion: print it as-is. */
            uart_putc('%');
            uart_putc(*p);
            break;
        }
    }

    va_end(args);
}
