# x64 replacement for Wine's ARM64EC invoke_arm64ec_syscall, installed in memory by FEX (patch 0006).
#
# Wine runs an x64 `syscall` instruction by raising STATUS_EMULATION_SYSCALL; dispatch_syscall then resumes the
# x64 code at invoke_arm64ec_syscall with: rax = syscall number, rcx = 1st argument (copied from r10),
# rdx/r8/r9 = arguments 2-4, r10 = address after the syscall instruction, rsp = rsp at the syscall
# (arguments 5+ at [rsp+0x28]). Wine's version calls the syscall like a function and returns, so the guest gets
# rcx = whatever the callee left (measured: the status value), r10 = the return address, rdx clobbered.
#
# Hardware SYSCALL/SYSRET always returns rcx = return address and r11 = rflags. This version returns like that
# and keeps every other register as it was: rdx, r8, r9, r10 (= 1st argument, its value at the syscall),
# xmm0-xmm5, rflags and rsp.
#
# The 8-byte immediate 0x1122334455667788 is replaced with the address of Wine's arm64ec_syscalls table.
        .text
        .globl syscall_return_fix
syscall_return_fix:
        pushq %rbp
        movq %rsp, %rbp                 # rbp = rsp_at_syscall - 8
        pushfq                          # [rbp-0x08] rflags
        pushq %r10                      # [rbp-0x10] return address
        pushq %rcx                      # [rbp-0x18] 1st argument (= r10 at the syscall)
        pushq %rdx                      # [rbp-0x20]
        pushq %r8                       # [rbp-0x28]
        pushq %r9                       # [rbp-0x30]
        subq $0x60, %rsp                # [rbp-0x90] xmm0-xmm5
        movdqu %xmm0, 0x00(%rsp)
        movdqu %xmm1, 0x10(%rsp)
        movdqu %xmm2, 0x20(%rsp)
        movdqu %xmm3, 0x30(%rsp)
        movdqu %xmm4, 0x40(%rsp)
        movdqu %xmm5, 0x50(%rsp)
        andq $-16, %rsp
        subq $0x90, %rsp                # home space + 14 stack arguments
        .irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13
        movq 0x30+8*\i(%rbp), %r11      # argument 5+\i at [rsp_at_syscall + 0x28 + 8*\i]
        movq %r11, 0x20+8*\i(%rsp)
        .endr
        movabsq $0x1122334455667788, %r11
        callq *(%r11,%rax,8)
        movdqu -0x90(%rbp), %xmm0
        movdqu -0x80(%rbp), %xmm1
        movdqu -0x70(%rbp), %xmm2
        movdqu -0x60(%rbp), %xmm3
        movdqu -0x50(%rbp), %xmm4
        movdqu -0x40(%rbp), %xmm5
        movq -0x30(%rbp), %r9
        movq -0x28(%rbp), %r8
        movq -0x20(%rbp), %rdx
        movq -0x18(%rbp), %r10
        movq -0x10(%rbp), %rcx          # rcx = return address, as after SYSRET
        movq -0x08(%rbp), %r11          # r11 = rflags, as after SYSRET
        leaq -0x08(%rbp), %rsp
        popfq
        popq %rbp                       # rsp = rsp_at_syscall
        jmpq *%rcx
