#include "minemu/user_abi.h"

.syntax unified
.cpu cortex-a9
.arm

/*
 * int32_t ioctl(int fd, uint32_t request, ...)
 *
 * The ARM calling convention already puts the first four arguments in
 * r0-r3, even for a variadic function, so fd, request and up to two extra
 * arguments are where the kernel expects them. We only need to load the
 * syscall number into r7 (saving the caller's r7 first) and trap.
 * The kernel leaves the return value in r0.
 */
.section .text.ioctl, "ax", %progbits
.global ioctl
.type ioctl, %function
ioctl:
    push {r7, lr}
    mov r7, #MINEMU_SYSCALL_IOCTL
    svc #0
    pop {r7, pc}
