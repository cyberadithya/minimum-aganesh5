.syntax unified
.cpu cortex-a9
.arm

/*
 * IRQ trampoline, adapted from examples/irq-context-switch/irq.S.
 * The vector table (src/startup/vectors.S) branches here on every IRQ.
 */

/* Offsets into struct minemu_trap_frame (see minemu/trap.h). */
.equ FRAME_SIZE,          88
.equ FRAME_RETURN_LR,     52
.equ FRAME_SPSR,          56
.equ FRAME_EXCEPTION_ID,  60
.equ FRAME_FAULT_PC,      64
.equ FRAME_DFSR,          68
.equ FRAME_DFAR,          72
.equ FRAME_USER_SP,       76
.equ FRAME_RESERVED,      84

/* Interrupt controller registers (see minemu/platform.h). */
.equ INTC_BASE,           0x10000000
.equ INTC_CLAIM,          0x08

.global minemu_irq_trampoline
.type minemu_irq_trampoline, %function
.extern minemu_irq_dispatch

minemu_irq_trampoline:
    /* Allocate one trap frame on the IRQ stack. */
    sub sp, sp, #FRAME_SIZE

    /* Save r0-r12, the IRQ lr, and spsr (the interrupted cpsr). */
    stmia sp, {r0-r12}
    str lr, [sp, #FRAME_RETURN_LR]
    mrs r1, spsr
    str r1, [sp, #FRAME_SPSR]

    /* Read CLAIM to get the active source ID. */
    ldr r1, =INTC_BASE
    ldr r1, [r1, #INTC_CLAIM]
    str r1, [sp, #FRAME_EXCEPTION_ID]

    /* IRQ lr is the next instruction + 4, so the interrupted
     * instruction's address is lr - 4. */
    sub r1, lr, #4
    str r1, [sp, #FRAME_FAULT_PC]

    /* No fault status/address for an IRQ. */
    mov r1, #0
    str r1, [sp, #FRAME_DFSR]
    str r1, [sp, #FRAME_DFAR]

    /* Save the banked sp and lr ('^' selects the user/system bank). */
    add r1, sp, #FRAME_USER_SP
    stmia r1, {sp, lr}^
    mov r1, #0
    str r1, [sp, #FRAME_RESERVED]

    /* minemu_irq_dispatch(frame) */
    mov r0, sp
    bl minemu_irq_dispatch

    /* Restore from the frame the dispatcher returned. */
    mov sp, r0
    ldr lr, [sp, #FRAME_RETURN_LR]
    ldr r1, [sp, #FRAME_SPSR]
    msr spsr_cxsf, r1
    add r1, sp, #FRAME_USER_SP
    ldmia r1, {sp, lr}^
    ldmia sp, {r0-r12}
    add sp, sp, #FRAME_SIZE

    /* Return to the interrupted instruction: pc = lr - 4, cpsr = spsr. */
    subs pc, lr, #4
