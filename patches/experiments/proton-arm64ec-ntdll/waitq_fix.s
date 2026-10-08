// Wine ARM64EC ntdll: make the RtlWaitOnAddress / RtlWakeAddress* futex-queue spinlock
// suspension-safe. A thread suspended (NtSuspendThread) while it holds a queue spinlock blocks every
// other thread that touches the same bucket; if the suspender is one of them, it spins forever.
// While the lock is held we set ChpeV2CpuAreaInfo->InSyscallCallback = 2, which makes Wine's
// SIGUSR1 handler use the cooperative suspend path; on unlock we clear it and complete a pending
// suspend the same way leave_syscall_callback() does (RtlCaptureContext + NtContinue).
//
// Placed over the ARM64EC copy of DbgUiConvertStateChangeStructure (RVA 0xb491c, 712 bytes).
        .text
        .set BASE, 0x1800b491c
        .set RTL_CAPTURE_CONTEXT, 0x1800c8d10
        .set NT_CONTINUE, 0x1800c6a0c        // #NtContinue$hp_target
        .set CHPE, 0x1788                     // TEB->ChpeV2CpuAreaInfo

cave_start:

// x16 = lock address, x17 = return address. Preserves everything but x16, x17.
common_lock:
        stp     x0, x1, [sp, #-16]!
        ldr     x0, [x18, #CHPE]
        cbz     x0, 1f
        ldrb    w1, [x0, #1]                  // InSyscallCallback
        cbnz    w1, 1f                        // already set (by FEX): leave it alone
        mov     w1, #2
        strb    w1, [x0, #1]
1:      mov     w1, #-1
2:      ldaxr   w0, [x16]
        cbnz    w0, 3f
        stlxr   w0, w1, [x16]
        cbnz    w0, 2b
        b       4f
3:      clrex
        yield
        b       2b
4:      dmb     ish
        ldp     x0, x1, [sp], #16
        br      x17

// x16 = lock address, x17 = return address. Preserves everything but x16 (x17 kept).
common_unlock:
        stp     x0, x1, [sp, #-16]!
        stlr    wzr, [x16]
        dmb     ish
        ldr     x0, [x18, #CHPE]
        cbz     x0, 9f
        ldrb    w1, [x0, #1]
        eor     w1, w1, #2
        cbnz    w1, 9f                        // not set by us
        strb    wzr, [x0, #1]
        ldrb    w1, [x0]                      // InSimulation
        cbnz    w1, 9f
        ldr     x1, [x0, #0x20]               // SuspendDoorbell
        cbz     x1, 9f
        ldr     w1, [x1]
        cbnz    w1, 5f
9:      ldp     x0, x1, [sp], #16
        br      x17

        // a suspend arrived while we held the lock: complete it now
5:      sub     sp, sp, #0x5e0
        stp     x2, x3, [sp, #0x00]
        stp     x4, x5, [sp, #0x10]
        stp     x6, x7, [sp, #0x20]
        stp     x8, x9, [sp, #0x30]
        stp     x10, x11, [sp, #0x40]
        stp     x12, x15, [sp, #0x50]
        stp     x16, x17, [sp, #0x60]
        stp     x19, x20, [sp, #0x70]
        stp     x21, x22, [sp, #0x80]
        stp     x25, x26, [sp, #0x90]
        stp     x27, x29, [sp, #0xa0]
        str     x30, [sp, #0xb0]
        mrs     x2, nzcv
        str     x2, [sp, #0xb8]
        str     xzr, [sp, #0xc0]              // "already continued" guard
        add     x0, sp, #0x100                // CONTEXT, 0x4d0 bytes
        bl      cave_start + (RTL_CAPTURE_CONTEXT - BASE)
        ldr     x1, [sp, #0xc0]
        cbnz    x1, 6f
        ldr     x0, [x18, #CHPE]
        ldr     x1, [x0, #0x20]
        ldr     w1, [x1]
        cbz     w1, 6f
        mov     x1, #1
        str     x1, [sp, #0xc0]
        add     x0, sp, #0x100
        mov     w1, #0
        bl      cave_start + (NT_CONTINUE - BASE)
6:      ldr     x2, [sp, #0xb8]
        msr     nzcv, x2
        ldp     x2, x3, [sp, #0x00]
        ldp     x4, x5, [sp, #0x10]
        ldp     x6, x7, [sp, #0x20]
        ldp     x8, x9, [sp, #0x30]
        ldp     x10, x11, [sp, #0x40]
        ldp     x12, x15, [sp, #0x50]
        ldp     x16, x17, [sp, #0x60]
        ldp     x19, x20, [sp, #0x70]
        ldp     x21, x22, [sp, #0x80]
        ldp     x25, x26, [sp, #0x90]
        ldp     x27, x29, [sp, #0xa0]
        ldr     x30, [sp, #0xb0]
        add     sp, sp, #0x5e0
        b       9b

// per-site stubs: \name, lock register, scratch register to zero, return address
        .macro LOCKSTUB name, lreg, sreg, ret
\name:  mov     x16, \lreg
        adr     x17, cave_start + (\ret - BASE)
        mov     \sreg, wzr
        b       common_lock
        .endm
        .macro UNLOCKSTUB name, lreg, sreg, ret
\name:  mov     x16, \lreg
        adr     x17, cave_start + (\ret - BASE)
        mov     \sreg, wzr
        b       common_unlock
        .endm

        LOCKSTUB   stub_L1, x8,  w12, 0x1800cda34   // RtlWakeAddressSingle
        LOCKSTUB   stub_L2, x26, w10, 0x1800ce1c8   // RtlWaitOnAddress (1st)
        LOCKSTUB   stub_L3, x26, w9,  0x1800ce2e0   // RtlWaitOnAddress (2nd)
        LOCKSTUB   stub_L4, x20, w9,  0x1800ce558   // RtlWakeAddressAll
        UNLOCKSTUB stub_U1, x8,  w9,  0x1800cda80   // RtlWakeAddressSingle
        UNLOCKSTUB stub_U2, x8,  w9,  0x1800cdaa4
        UNLOCKSTUB stub_U3, x26, w8,  0x1800ce25c   // RtlWaitOnAddress
        UNLOCKSTUB stub_U4, x26, w8,  0x1800ce28c
        UNLOCKSTUB stub_U5, x26, w8,  0x1800ce304
        UNLOCKSTUB stub_U6, x20, w8,  0x1800ce5e4   // RtlWakeAddressAll
        UNLOCKSTUB stub_U7, x20, w8,  0x1800ce610
cave_end:
