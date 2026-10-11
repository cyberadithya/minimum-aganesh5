.syntax unified
.cpu cortex-a9
.arm

/*
 * void user_enter(uint32_t entry, uint32_t stack_top)
 * Adapted from examples/user-mode-hello/kernel/user.S.
 *
 * r0 is the A32 entry VA and r1 is the top of the user stack.
 * The first switch into user mode works like returning from an exception:
 * put the target in lr and the new mode in spsr, then "movs pc, lr".
 */
.global user_enter
.type user_enter, %function
user_enter:
    cpsid if
    mov r2, r0

    /* USR mode has its own banked sp and lr. ldm with '^' writes the
     * user-bank copies, so load sp = stack_top and lr = 0 that way. */
    sub sp, sp, #8
    str r1, [sp]
    mov r3, #0
    str r3, [sp, #4]
    mov r3, sp
    ldmia r3, {sp, lr}^
    add sp, sp, #8

    /* spsr = USR mode (0x10), ARM state, IRQs unmasked. */
    mov r3, #0x10
    msr spsr_cxsf, r3

    /* Don't leak kernel values into the user registers. */
    mov lr, r2
    mov r0, #0
    mov r1, #0
    mov r2, #0
    mov r3, #0

    /* pc = entry and cpsr = spsr: now running in user mode. */
    movs pc, lr
