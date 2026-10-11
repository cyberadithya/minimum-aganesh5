#include <stdint.h>

#include "minemu/user_abi.h"

void minemu_user_main(void) {
    static char message[] = "hello world\n";

    /* User mode can't touch the UART registers directly, so ask the
     * kernel to write the message for us. */
    (void)ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)message,
                (int)(sizeof(message) - 1U));

    for (;;) {
        __asm__ volatile("nop");
    }
}
