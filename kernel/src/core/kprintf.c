#include <stdarg.h>
#include <stdint.h>

#include "kprintf.h"
#include "uart.h"

static void print_unsigned(unsigned int value, unsigned int base) {
    const char digits[] = "0123456789abcdef";
    char buf[11]; /* enough for a 32-bit value in base 10 or 16 */
    int i = 0;

    /* Digits come out least significant first, so store them and
     * print in reverse. */
    do {
        buf[i++] = digits[value % base];
        value /= base;
    } while (value != 0);

    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

static void print_signed(int value) {
    if (value < 0) {
        uart_putc('-');
        /* Negate as unsigned so the most negative int still works. */
        print_unsigned(0u - (unsigned int)value, 10);
    } else {
        print_unsigned((unsigned int)value, 10);
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
            print_unsigned(va_arg(args, unsigned int), 10);
            break;
        case 'x':
            print_unsigned(va_arg(args, unsigned int), 16);
            break;
        case 'p':
            uart_puts("0x");
            print_unsigned((unsigned int)(uintptr_t)va_arg(args, void *), 16);
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
