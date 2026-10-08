# Replacement for Wine ARM64EC invoke_arm64ec_syscall (x64 code).
# Entry (set up by dispatch_syscall): rsp = stack at the syscall instruction (points at the
# stub's return address), rcx = 1st arg (from r10), rdx/r8/r9 = args 2-4, args 5+ at [rsp+0x28],
# r10 = return address after the syscall instruction, rax = syscall number.
# Exit: like a Windows kernel syscall return: rax = status, rcx = return rip, r11 = rflags,
# every other register (rdx, r8, r9, r10 included) and rflags unchanged, rsp unchanged.
        .text
        .globl invoke
invoke:
        pushq %rbp
        movq %rsp, %rbp
        pushfq                      # [rbp-0x08] rflags
        pushq %rcx                  # [rbp-0x10] r10 at syscall time (1st arg)
        pushq %rdx                  # [rbp-0x18]
        pushq %r8                   # [rbp-0x20]
        pushq %r9                   # [rbp-0x28]
        pushq %r10                  # [rbp-0x30] return address
        andq $-16, %rsp
        subq $0x90, %rsp            # home space + 14 stack arguments
        .irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13
        movq 0x30+8*\i(%rbp), %r11
        movq %r11, 0x20+8*\i(%rsp)
        .endr
        leaq table_placeholder(%rip), %r11
        callq *(%r11,%rax,8)
        leaq -0x30(%rbp), %rsp
        popq %rcx                   # rcx = return rip
        popq %r9
        popq %r8
        popq %rdx
        popq %r10
        movq (%rsp), %r11           # r11 = rflags
        popfq
        popq %rbp
        jmpq *%rcx
table_placeholder:
