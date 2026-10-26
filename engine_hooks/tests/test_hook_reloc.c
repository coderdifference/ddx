// Offline tests for moving copied code with a RIP-relative operand (hook_reloc.c).
#include <stdio.h>
#include <string.h>
#include "../src/common.h"

static int g_failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x);                                                         \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

static int32_t disp_at(const uint8_t *code, size_t at) {
    int32_t d;
    memcpy(&d, code + at, 4);
    return d;
}

// Raid::ApplyBurnDot's first 18 bytes (macOS): push rbp; mov rbp,rsp; push r14; push rbx; sub rsp,0x50;
// mov rax,[rip+0x9697e6] (disp32 at 14, instruction ends at 18): ___stack_chk_guard's pointer
static const uint8_t burn[] = {0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53, 0x48, 0x83,
                               0xec, 0x50, 0x48, 0x8b, 0x05, 0xe6, 0x97, 0x96, 0x00};

static void check_move(uintptr_t from, uintptr_t to) {
    uint8_t code[sizeof burn];
    memcpy(code, burn, sizeof burn);
    CHECK(hook_relocate_rip(code, sizeof code, 14, 18, from, to));
    CHECK(to + 18 + disp_at(code, 14) == from + 18 + 0x9697e6); // the same operand from the new place
    CHECK(!memcmp(code, burn, 14));                             // nothing else changes
}

int main(void) {
    const uintptr_t at = 0x10094d950;
    check_move(at, at);              // not moved: unchanged
    check_move(at, 0x101200000);     // a page above the image
    check_move(at, 0x100800000);     // below the code
    check_move(at, at + 0x7f000000); // near the edge of reach, both ways
    check_move(at, at - 0x7f000000);

    uint8_t code[32];
    memcpy(code, burn, sizeof burn);
    CHECK(hook_relocate_rip(code, sizeof burn, 0, 0, at, 0x200000000)); // no operand: always fine
    CHECK(!memcmp(code, burn, sizeof burn));
    CHECK(!hook_relocate_rip(code, sizeof burn, 14, 18, at, at + 0x90000000)); // out of rel32 reach
    CHECK(!hook_relocate_rip(code, sizeof burn, 14, 18, at, at - 0x90000000));
    CHECK(!memcmp(code, burn, sizeof burn));                           // left alone when refused
    CHECK(!hook_relocate_rip(code, sizeof burn, 15, 18, at, at + 16)); // disp32 doesn't fit its instruction
    CHECK(!hook_relocate_rip(code, sizeof burn, 14, 19, at, at + 16)); // instruction past the copied bytes

    // an immediate after the disp32 (cmp byte [rip+disp32], imm8: 80 3d d d d d ib) is relative to the end
    const uint8_t cmp[] = {0x80, 0x3d, 0x10, 0x00, 0x00, 0x00, 0x01};
    memcpy(code, cmp, sizeof cmp);
    CHECK(hook_relocate_rip(code, sizeof cmp, 2, 7, 0x1000, 0x2000));
    CHECK(disp_at(code, 2) == 0x10 - 0x1000 && code[6] == 0x01);

    // a call rel32 is relative the same way (e8 d d d d: disp32 at 1, ends at 5)
    const uint8_t call[] = {0xe8, 0x00, 0x01, 0x00, 0x00};
    memcpy(code, call, sizeof call);
    CHECK(hook_relocate_rip(code, sizeof call, 1, 5, 0x100a07f54, 0x101300000));
    CHECK(0x101300000 + 5 + disp_at(code, 1) == 0x100a07f54 + 5 + 0x100);

    // DoesHeroMeetRequirements: two operands (disp32 at 9 and 16), each fixed on its own
    const uint8_t two[] = {0x55, 0x48, 0x89, 0xe5, 0x53, 0x50, 0x48, 0x8b, 0x05, 0x6b,
                           0x07, 0x83, 0x02, 0x48, 0x8b, 0x0d, 0x6c, 0x07, 0x83, 0x02};
    memcpy(code, two, sizeof two);
    CHECK(hook_relocate_rip(code, sizeof two, 9, 13, 0x1005bf450, 0x101300000) &&
          hook_relocate_rip(code, sizeof two, 16, 20, 0x1005bf450, 0x101300000));
    CHECK(0x101300000 + 13 + disp_at(code, 9) == 0x1005bf450 + 13 + 0x283076b);
    CHECK(0x101300000 + 20 + disp_at(code, 16) == 0x1005bf450 + 20 + 0x283076c);

    // a negative displacement
    const uint8_t lea[] = {0x48, 0x8d, 0x05, 0x00, 0xff, 0xff, 0xff}; // lea rax,[rip-0x100]
    memcpy(code, lea, sizeof lea);
    CHECK(hook_relocate_rip(code, sizeof lea, 3, 7, 0x5000, 0x4000));
    CHECK(disp_at(code, 3) == -0x100 + 0x1000);

    // hook_relocate: the whole copy, rel8 jumps grown into rel32 ones
    uint8_t out[64];
    // CurioInteractionPurgeResult::Apply: cmp byte [rdi+0x18],0; je +0x44; push rbp; mov rbp,rsp; sub rsp,0x40;
    // mov rax,[rdi+0x10] (14 bytes; the je at 4, its rel8 at 5)
    const uint8_t purge[] = {0x80, 0x7f, 0x18, 0x00, 0x74, 0x44, 0x55, 0x48, 0x89,
                             0xe5, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b, 0x47, 0x10};
    const uintptr_t pf = 0x1004f11b0, pt = 0x101300000;
    size_t n = hook_relocate(purge, sizeof purge, (const HookRip[]){{5, 6}}, 1, pf, out, sizeof out, pt);
    CHECK(n == sizeof purge + 4); // je rel8 (2 bytes) -> je rel32 (6)
    CHECK(!memcmp(out, purge, 4) && out[4] == 0x0f && out[5] == 0x84);
    CHECK(pt + 10 + disp_at(out, 6) == pf + 6 + 0x44);     // the same target as the original je
    CHECK(!memcmp(out + 10, purge + 6, sizeof purge - 6)); // the rest unchanged

    // jmp rel8 -> e9 rel32, a negative rel8, and a rel32 after it (shifted by the growth)
    const uint8_t mix[] = {0xeb, 0xf0, 0x48, 0x8b, 0x05, 0x00, 0x10, 0x00, 0x00}; // jmp -0x10; mov rax,[rip+0x1000]
    n = hook_relocate(mix, sizeof mix, (const HookRip[]){{1, 2}, {5, 9}}, 2, 0x5000, out, sizeof out, 0x9000);
    CHECK(n == sizeof mix + 3 && out[0] == 0xe9);
    CHECK(0x9000 + 5 + disp_at(out, 1) == 0x5000 + 2 - 0x10);
    CHECK(0x9000 + 12 + disp_at(out, 8) == 0x5000 + 9 + 0x1000);

    // Battle::SetState: test esi,esi; je +0xf; cmp esi,0x29; jg +0xa; cmp byte [rdi+0x2da],0 (two rel8 jumps, both
    // to +0x13, past the 16 bytes)
    const uint8_t set_state[] = {0x85, 0xf6, 0x74, 0x0f, 0x83, 0xfe, 0x29, 0x7f,
                                 0x0a, 0x80, 0xbf, 0xda, 0x02, 0x00, 0x00, 0x00};
    const uintptr_t sf = 0x100851420, st = 0x101400000;
    n = hook_relocate(set_state, sizeof set_state, (const HookRip[]){{3, 4}, {8, 9}}, 2, sf, out, sizeof out, st);
    CHECK(n == sizeof set_state + 8); // two rel8 jumps, 4 bytes longer each
    CHECK(out[2] == 0x0f && out[3] == 0x84 && st + 8 + disp_at(out, 4) == sf + 0x13);
    CHECK(out[11] == 0x0f && out[12] == 0x8f && st + 17 + disp_at(out, 13) == sf + 0x13);
    CHECK(!memcmp(out + 17, set_state + 9, 7));

    // refused: a branch back into the copied bytes, jrcxz (no rel32 form), operands out of order, too little room
    const uint8_t back[] = {0x74, 0x02, 0x90, 0x90, 0x90}; // je +2: lands at 4, inside
    CHECK(!hook_relocate(back, sizeof back, (const HookRip[]){{1, 2}}, 1, 0x5000, out, sizeof out, 0x9000));
    const uint8_t jrcxz[] = {0xe3, 0x40};
    CHECK(!hook_relocate(jrcxz, sizeof jrcxz, (const HookRip[]){{1, 2}}, 1, 0x5000, out, sizeof out, 0x9000));
    CHECK(!hook_relocate(mix, sizeof mix, (const HookRip[]){{5, 9}, {1, 2}}, 2, 0x5000, out, sizeof out, 0x9000));
    CHECK(!hook_relocate(purge, sizeof purge, (const HookRip[]){{5, 6}}, 1, pf, out, 8, pt));
    // no operands: a plain copy
    CHECK(hook_relocate(burn, sizeof burn, NULL, 0, at, out, sizeof out, 0x200000000) == sizeof burn &&
          !memcmp(out, burn, sizeof burn));

    printf(g_failures ? "hook_reloc: %d failure(s)\n" : "hook_reloc: all passed\n", g_failures);
    return g_failures != 0;
}
