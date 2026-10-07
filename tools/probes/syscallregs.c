// syscallregs: what does a raw x64 `syscall` instruction leave in the registers?
//
// On x86-64 hardware SYSCALL loads rcx with the return address and r11 with rflags, and SYSRET returns through
// them, so after any syscall rcx == the address of the next instruction and r11 == rflags. A protector that
// issues raw syscalls can check that. This probe puts marker values in the volatile registers, issues one raw
// NtYieldExecution syscall (number read from ntdll's own x64 stub), and reports every register afterwards.
// Output: D:\aoe\syscallregs.txt. Build: x86_64-w64-mingw32-gcc -O1 -static -o syscallregs.exe syscallregs.c
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

struct regs {
    uint64_t rax, rcx, rdx, r8, r9, r10, r11, flags_before, ret_addr;
    uint64_t xmm[6][2];
};

static int ssn_of(const char *name, unsigned char *bytes_out)
{
    unsigned char *p = (unsigned char *)GetProcAddress(GetModuleHandleA("ntdll.dll"), name);
    if (!p) return -1;
    memcpy(bytes_out, p, 16);
    // Windows/Wine x64 syscall stub: 4c 8b d1 (mov r10, rcx) b8 xx xx xx xx (mov eax, imm32)
    if (p[0] == 0x4c && p[1] == 0x8b && p[2] == 0xd1 && p[3] == 0xb8) return *(int *)(p + 4);
    return -2;
}

int main(void)
{
    FILE *f = fopen("D:\\aoe\\syscallregs.txt", "w");
    if (!f) return 1;
    unsigned char b[16];
    int ssn = ssn_of("NtYieldExecution", b);
    fprintf(f, "NtYieldExecution x64 stub bytes:");
    for (int i = 0; i < 16; i++) fprintf(f, " %02x", b[i]);
    fprintf(f, "\nssn = %d (0x%x)\n", ssn, ssn);
    if (ssn < 0) { fclose(f); return 0; }

    struct regs r;
    memset(&r, 0, sizeof(r));
    uint64_t xin[6][2];
    for (int i = 0; i < 6; i++) { xin[i][0] = 0x1111111111111111ULL * (i + 1); xin[i][1] = 0xA0A0A0A0A0A0A0A0ULL + i; }

    __asm__ volatile(
        "movdqu 0x00(%[xin]), %%xmm0\n\t"
        "movdqu 0x10(%[xin]), %%xmm1\n\t"
        "movdqu 0x20(%[xin]), %%xmm2\n\t"
        "movdqu 0x30(%[xin]), %%xmm3\n\t"
        "movdqu 0x40(%[xin]), %%xmm4\n\t"
        "movdqu 0x50(%[xin]), %%xmm5\n\t"
        "pushfq\n\t"
        "popq %%r11\n\t"
        "movq %%r11, %c[fb](%[out])\n\t"
        "leaq 1f(%%rip), %%r11\n\t"
        "movq %%r11, %c[ra](%[out])\n\t"
        "movq $0xDDDD0001DDDD0001, %%rdx\n\t"
        "movq $0x8888000188880001, %%r8\n\t"
        "movq $0x9999000199990001, %%r9\n\t"
        "movq $0xCCCC0001CCCC0001, %%rcx\n\t"
        "movq %%rcx, %%r10\n\t"
        "movq $0xBBBB0001BBBB0001, %%r11\n\t"
        "movl %[ssn], %%eax\n\t"
        "syscall\n"
        "1:\n\t"
        "movq %%rax, %c[rax](%[out])\n\t"
        "movq %%rcx, %c[rcx](%[out])\n\t"
        "movq %%rdx, %c[rdx](%[out])\n\t"
        "movq %%r8, %c[r8](%[out])\n\t"
        "movq %%r9, %c[r9](%[out])\n\t"
        "movq %%r10, %c[r10](%[out])\n\t"
        "movq %%r11, %c[r11](%[out])\n\t"
        "movdqu %%xmm0, %c[x0](%[out])\n\t"
        "movdqu %%xmm1, %c[x1](%[out])\n\t"
        "movdqu %%xmm2, %c[x2](%[out])\n\t"
        "movdqu %%xmm3, %c[x3](%[out])\n\t"
        "movdqu %%xmm4, %c[x4](%[out])\n\t"
        "movdqu %%xmm5, %c[x5](%[out])\n\t"
        :
        : [out] "r"(&r), [xin] "r"(xin), [ssn] "r"(ssn),
          [rax] "i"(offsetof(struct regs, rax)), [rcx] "i"(offsetof(struct regs, rcx)), [rdx] "i"(offsetof(struct regs, rdx)),
          [r8] "i"(offsetof(struct regs, r8)), [r9] "i"(offsetof(struct regs, r9)), [r10] "i"(offsetof(struct regs, r10)),
          [r11] "i"(offsetof(struct regs, r11)), [fb] "i"(offsetof(struct regs, flags_before)),
          [ra] "i"(offsetof(struct regs, ret_addr)),
          [x0] "i"(offsetof(struct regs, xmm[0])), [x1] "i"(offsetof(struct regs, xmm[1])), [x2] "i"(offsetof(struct regs, xmm[2])),
          [x3] "i"(offsetof(struct regs, xmm[3])), [x4] "i"(offsetof(struct regs, xmm[4])), [x5] "i"(offsetof(struct regs, xmm[5]))
        : "rax", "rcx", "rdx", "r8", "r9", "r10", "r11", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "memory", "cc");

    fprintf(f, "\nstatus rax = 0x%llx\n", (unsigned long long)r.rax);
    fprintf(f, "rcx = 0x%016llx   hardware: return address 0x%016llx -> %s\n", (unsigned long long)r.rcx,
            (unsigned long long)r.ret_addr, r.rcx == r.ret_addr ? "MATCH" : "DIFFERENT");
    fprintf(f, "r11 = 0x%016llx   hardware: rflags (before: 0x%llx) -> %s\n", (unsigned long long)r.r11,
            (unsigned long long)r.flags_before, (r.r11 & 0xfd5) == (r.flags_before & 0xfd5) ? "MATCH (flag bits)" : "DIFFERENT");
    fprintf(f, "rdx = 0x%016llx   in 0xDDDD0001DDDD0001 -> %s\n", (unsigned long long)r.rdx, r.rdx == 0xDDDD0001DDDD0001ULL ? "kept" : "changed");
    fprintf(f, "r8  = 0x%016llx   in 0x8888000188880001 -> %s\n", (unsigned long long)r.r8, r.r8 == 0x8888000188880001ULL ? "kept" : "changed");
    fprintf(f, "r9  = 0x%016llx   in 0x9999000199990001 -> %s\n", (unsigned long long)r.r9, r.r9 == 0x9999000199990001ULL ? "kept" : "changed");
    fprintf(f, "r10 = 0x%016llx   in 0xCCCC0001CCCC0001 -> %s\n", (unsigned long long)r.r10, r.r10 == 0xCCCC0001CCCC0001ULL ? "kept" : "changed");
    for (int i = 0; i < 6; i++)
        fprintf(f, "xmm%d = %016llx%016llx -> %s\n", i, (unsigned long long)r.xmm[i][1], (unsigned long long)r.xmm[i][0],
                (r.xmm[i][0] == xin[i][0] && r.xmm[i][1] == xin[i][1]) ? "kept" : "changed");
    fprintf(f, "END\n");
    fclose(f);
    return 0;
}
