#include "minemu/boot.h"
#include "minemu/trap.h"
#include "minemu/trace.h"
#include "kprintf.h"
#include "uart.h"
#include "user.h"

void minemu_kernel_main(const struct minemu_boot_info *boot_info) {
    if ((uintptr_t)boot_info != MINEMU_BOOT_INFO_VADDR ||
        boot_info->magic != MINEMU_BOOT_INFO_MAGIC ||
        boot_info->version != MINEMU_ABI_VERSION ||
        boot_info->size != sizeof(*boot_info) ||
        boot_info->system_rom_base != UINT32_C(0x08000000) ||
        boot_info->direct_map_vaddr != UINT32_C(0xc0000000) ||
        boot_info->direct_map_paddr != UINT32_C(0x40000000) ||
        boot_info->direct_map_size != UINT32_C(0x04000000)) {
        minemu_trace_event(UINT32_C(0xb007bad0));
        minemu_fail_stop();
    }

    /* Turn on UART0 receive interrupts. CPU IRQs get unmasked when we
     * drop to user mode (user_enter sets spsr with the I bit clear). */
    uart_init();

    uint32_t entry;
    if (!user_load(boot_info, "minimum-user", &entry)) {
        kprintf("kernel: could not load minimum-user\n");
        minemu_trace_event(UINT32_C(0xbad10ad0));
        minemu_fail_stop();
    }

    user_enter(entry, USER_STACK_TOP);
}
