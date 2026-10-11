.syntax unified
.cpu cortex-a9
.arm

/*
 * SVC trampoline. The vector table (src/startup/vectors.S) branches here
 * when user code executes "svc #0". The hardware has already switched to
 * SVC mode, saved the user CPSR in spsr_svc, put the address of the
 * instruction after svc in lr_svc, and masked IRQs.
 *
 * Same frame layout as irq.S (struct minemu_trap_frame in minemu/trap.h).
 */

.equ FRAME_SIZE,          88
.equ FRAME_RETURN_LR,     52
.equ FRAME_SPSR,          56
.equ FRAME_EXCEPTION_ID,  60
.equ FRAME_FAULT_PC,      64
.equ FRAME_DFSR,          68
.equ FRAME_DFAR,          72
.equ FRAME_USER_SP,       76
.equ FRAME_RESERVED,      84

.equ EXCEPTION_SVC,       -3

.global minemu_svc_trampoline
.type minemu_svc_trampoline, %function
.extern minemu_svc_dispatch

minemu_svc_trampoline:
    /* Allocate one frame on the SVC stack. */
    sub sp, sp, #FRAME_SIZE

    /* Save r0-r12 (r0-r3 and r7 hold the syscall arguments and number),
     * the return address, and spsr (the user's cpsr). */
    stmia sp, {r0-r12}
    str lr, [sp, #FRAME_RETURN_LR]
    mrs r1, spsr
    str r1, [sp, #FRAME_SPSR]

    ldr r1, =EXCEPTION_SVC
    str r1, [sp, #FRAME_EXCEPTION_ID]

    /* lr_svc points to the instruction after svc, so the svc
     * instruction itself is at lr - 4. */
    sub r1, lr, #4
    str r1, [sp, #FRAME_FAULT_PC]

    /* No fault information for a syscall. */
    mov r1, #0
    str r1, [sp, #FRAME_DFSR]
    str r1, [sp, #FRAME_DFAR]

    /* Save the user-mode sp and lr ('^' selects the user bank). */
    add r1, sp, #FRAME_USER_SP
    stmia r1, {sp, lr}^
    mov r1, #0
    str r1, [sp, #FRAME_RESERVED]

    /* minemu_svc_dispatch(frame). It puts the result in frame->r[0]. */
    mov r0, sp
    bl minemu_svc_dispatch

    /* Restore everything from the frame the dispatcher returned. */
    mov sp, r0
    ldr lr, [sp, #FRAME_RETURN_LR]
    ldr r1, [sp, #FRAME_SPSR]
    msr spsr_cxsf, r1
    add r1, sp, #FRAME_USER_SP
    ldmia r1, {sp, lr}^
    ldmia sp, {r0-r12}
    add sp, sp, #FRAME_SIZE

    /* Return to the instruction after svc: pc = lr, cpsr = spsr.
     * (Unlike IRQ, no -4 adjustment, since svc should not run again.) */
    movs pc, lr
