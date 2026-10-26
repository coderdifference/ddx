// Compare sites (see common.h, install_cmp_site): the stub that replaces one `cmp r32, [rip+disp32]` instruction.
// Pure code generation, shared by the installer (hook.c) and the offline tests.
//
// The site's instruction becomes `jmp rel32 stub; nop` and the stub, within rel32 reach, runs:
//   push rax, rcx, rdx, rsi, rdi, r8, r9, r10, r11, rbx   the volatile registers, and rbx (the stack pointer's keeper)
//   mov rbx, rsp; and rsp, -16                             a call needs a 16-byte aligned stack
//   mov esi, <register>                                    the compared register (rbx: its saved copy, [rbx])
//   mov rdi, ud; mov rdx, &operand; mov edx, [rdx]         fn(ud, register value, memory operand value)
//   mov rax, fn; call rax
//   cmp al, 1                                              ZF set when fn returned true; mov and pop keep the flags
//   mov rsp, rbx; pop rbx, r11, ..., rax
//   jmp rel32 <the instruction after the site>
#include "common.h"
#include <string.h>

int hook_cmp_register(const uint8_t *insn, size_t len) {
    size_t i = 0;
    int r = 0;
    if (len == 7) { // REX: 0100WRXB, W clear (32-bit), only R used
        if ((insn[0] & 0xF0) != 0x40 || (insn[0] & 0x0B))
            return -1;
        r = insn[0] & 0x04 ? 8 : 0;
        i = 1;
    } else if (len != 6)
        return -1;
    if (insn[i] != 0x3B || (insn[i + 1] & 0xC7) != 0x05)
        return -1; // cmp r32, r/m32 with mod 00, rm 101: [rip+disp32]
    r |= (insn[i + 1] >> 3) & 7;
    return r == 4 ? -1 : r; // esp: not a value
}

size_t hook_cmp_stub(uint8_t *out, size_t cap, const uint8_t *insn, size_t len, uintptr_t site, uintptr_t at,
                     HookCmpFn fn, void *ud) {
    int r = hook_cmp_register(insn, len);
    if (r < 0 || cap < 128)
        return 0;
    int32_t disp;
    memcpy(&disp, insn + len - 4, 4);
    uint64_t operand = (uint64_t)(site + len) + (int64_t)disp;
    uint8_t *p = out;
    static const uint8_t save[] = {0x50, 0x51, 0x52, 0x56, 0x57, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52,
                                   0x41, 0x53, 0x53, 0x48, 0x89, 0xE3, 0x48, 0x83, 0xE4, 0xF0};
    memcpy(p, save, sizeof save);
    p += sizeof save;
    if (r == 3) {
        *p++ = 0x8B;
        *p++ = 0x33;
    } // mov esi, [rbx]
    else {
        if (r >= 8)
            *p++ = 0x44; // REX.R
        *p++ = 0x89;
        *p++ = (uint8_t)(0xC0 | (r & 7) << 3 | 6); // mov esi, r32
    }
    uint64_t v = (uint64_t)ud;
    *p++ = 0x48;
    *p++ = 0xBF;
    memcpy(p, &v, 8);
    p += 8; // mov rdi, ud
    *p++ = 0x48;
    *p++ = 0xBA;
    memcpy(p, &operand, 8);
    p += 8; // mov rdx, &operand
    *p++ = 0x8B;
    *p++ = 0x12; // mov edx, [rdx]
    v = (uint64_t)fn;
    *p++ = 0x48;
    *p++ = 0xB8;
    memcpy(p, &v, 8);
    p += 8; // mov rax, fn
    static const uint8_t back[] = {0xFF, 0xD0, 0x3C, 0x01, 0x48, 0x89, 0xDC, 0x5B, 0x41, 0x5B, 0x41,
                                   0x5A, 0x41, 0x59, 0x41, 0x58, 0x5F, 0x5E, 0x5A, 0x59, 0x58};
    memcpy(p, back, sizeof back); // call; cmp al,1; restore
    p += sizeof back;
    int64_t rel = (int64_t)(site + len) - (int64_t)(at + (p - out) + 5);
    if (rel != (int32_t)rel)
        return 0;
    int32_t rel32 = (int32_t)rel;
    *p++ = 0xE9;
    memcpy(p, &rel32, 4);
    p += 4; // jmp back
    return (size_t)(p - out);
}

// Windows (MSVC) also tests the other way round: `mov r32, [rip+disp32]; cmp [base+disp8], r32` (the constant into a
// register, then compared with a field). The site is both instructions: the register, the base and disp8 for that form.
int hook_cmp_pair(const uint8_t *insn, size_t len, int *base, int8_t *disp) {
    size_t i = 0;
    int r = 0;
    if (len < 9)
        return -1;
    if ((insn[0] & 0xF0) == 0x40) { // the mov's REX: only R
        if (insn[0] & 0x0B)
            return -1;
        r = insn[0] & 0x04 ? 8 : 0;
        i = 1;
    }
    if (insn[i] != 0x8B || (insn[i + 1] & 0xC7) != 0x05)
        return -1; // mov r32, [rip+disp32]
    r |= (insn[i + 1] >> 3) & 7;
    i += 6;
    int b = 0, rr = 0;
    if ((insn[i] & 0xF0) == 0x40) { // the cmp's REX: R and B
        if (insn[i] & 0x0A)
            return -1;
        rr = insn[i] & 0x04 ? 8 : 0;
        b = insn[i] & 0x01 ? 8 : 0;
        i++;
    }
    if (i + 3 != len || insn[i] != 0x39 || (insn[i + 1] & 0xC0) != 0x40)
        return -1; // cmp [base+disp8], r32
    rr |= (insn[i + 1] >> 3) & 7;
    b |= insn[i + 1] & 7;
    if (rr != r || b == r || (b & 7) == 4 || r == 4)
        return -1; // the same register, another base; rsp / r12 bases
                   // need a SIB byte
    *base = b;
    *disp = (int8_t)insn[i + 2];
    return r;
}

// The Win64 stub (MS ABI): fn(ud, value, operand) in rcx, edx, r8d with 32 bytes of shadow space; xmm0-5 kept too
// (volatile there, and the game's code may hold values in them across the site). The pair form first runs its mov
// (relocated), so the register holds the constant afterwards as in the game's code; mov and pop keep ZF.
size_t hook_cmp_stub_win64(uint8_t *out, size_t cap, const uint8_t *insn, size_t len, uintptr_t site, uintptr_t at,
                           HookCmpFn fn, void *ud) {
    int base = -1;
    int8_t d8 = 0;
    int r = hook_cmp_register(insn, len);
    size_t mov_len = 0;
    if (r < 0) {
        r = hook_cmp_pair(insn, len, &base, &d8);
        mov_len = insn[0] == 0x8B ? 6 : 7;
    }
    if (r < 0 || cap < 192)
        return 0;
    int32_t disp;
    memcpy(&disp, insn + (mov_len ? mov_len : len) - 4, 4);
    uint64_t operand = (uint64_t)(site + (mov_len ? mov_len : len)) + (int64_t)disp;
    uint8_t *p = out;
    if (mov_len) { // the game's mov, its disp32 fixed for here
        int64_t rel = (int64_t)operand - (int64_t)(at + mov_len);
        if (rel != (int32_t)rel)
            return 0;
        int32_t rel32 = (int32_t)rel;
        memcpy(p, insn, mov_len - 4);
        memcpy(p + mov_len - 4, &rel32, 4);
        p += mov_len;
    }
    static const uint8_t save[] = {
        0x50, 0x51, 0x52, 0x56, 0x57, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41,
        0x53, 0x53, 0x48, 0x89, 0xE3, 0x48, 0x83, 0xE4, 0xF0,                    // mov rbx, rsp; and rsp, -16
        0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00,                                // sub rsp, 0x80
        0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20, 0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30,  // movdqu
        0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40, 0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50,  // [rsp+0x20..],
        0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x60, 0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x70}; // xmm0-5
    memcpy(p, save, sizeof save);
    p += sizeof save;
    if (base < 0) { // edx = the register
        if (r == 3) {
            *p++ = 0x8B;
            *p++ = 0x13;
        } // mov edx, [rbx] (its saved copy)
        else {
            if (r >= 8)
                *p++ = 0x44;
            *p++ = 0x89;
            *p++ = (uint8_t)(0xC0 | (r & 7) << 3 | 2); // mov edx, r32
        }
    } else { // edx = [base+disp8]
        if (base == 3) {
            *p++ = 0x48;
            *p++ = 0x8B;
            *p++ = 0x03;
            base = 0;
        } // mov rax, [rbx] (rbx's saved copy)
        if (base >= 8)
            *p++ = 0x41;
        *p++ = 0x8B;
        *p++ = (uint8_t)(0x50 | (base & 7)); // mov edx, [base+disp8]
        *p++ = (uint8_t)d8;
    }
    uint64_t v = (uint64_t)ud;
    *p++ = 0x48;
    *p++ = 0xB9;
    memcpy(p, &v, 8);
    p += 8; // mov rcx, ud
    *p++ = 0x49;
    *p++ = 0xB8;
    memcpy(p, &operand, 8);
    p += 8; // mov r8, &operand
    *p++ = 0x45;
    *p++ = 0x8B;
    *p++ = 0x00; // mov r8d, [r8]
    v = (uint64_t)fn;
    *p++ = 0x48;
    *p++ = 0xB8;
    memcpy(p, &v, 8);
    p += 8;                                                // mov rax, fn
    static const uint8_t back[] = {0xFF, 0xD0, 0x3C, 0x01, // call rax; cmp al, 1
                                   0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20, 0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30, 0xF3, 0x0F,
                                   0x6F, 0x54, 0x24, 0x40, 0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50, 0xF3, 0x0F, 0x6F, 0x64,
                                   0x24, 0x60, 0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x70, 0x48, 0x89, 0xDC, 0x5B, 0x41, 0x5B,
                                   0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5F, 0x5E, 0x5A, 0x59, 0x58};
    memcpy(p, back, sizeof back);
    p += sizeof back;
    int64_t rel = (int64_t)(site + len) - (int64_t)(at + (p - out) + 5);
    if (rel != (int32_t)rel)
        return 0;
    int32_t rel32 = (int32_t)rel;
    *p++ = 0xE9;
    memcpy(p, &rel32, 4);
    p += 4; // jmp back
    return (size_t)(p - out);
}
