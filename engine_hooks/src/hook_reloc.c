// Moving copied code: the fix-up for a RIP-relative operand, shared by both installers (hook.c, hook_win.c).
#include "common.h"
#include <string.h>

bool hook_relocate_rip(uint8_t *code, size_t len, size_t rip_disp, size_t rip_next, uintptr_t from, uintptr_t to) {
    if (!rip_disp)
        return true; // nothing to fix
    if (rip_disp + 4 > rip_next || rip_next > len)
        return false;
    int32_t disp;
    memcpy(&disp, code + rip_disp, 4);
    // the operand's address (the end of its instruction + disp) stays put; only the code moves
    int64_t moved = (int64_t)(from + rip_next) + disp - (int64_t)(to + rip_next);
    if (moved != (int32_t)moved)
        return false; // out of rel32 reach from the new place
    int32_t d = (int32_t)moved;
    memcpy(code + rip_disp, &d, 4);
    return true;
}

// Copy `len` bytes of code (from `from`) to `out` (which runs at `to`), fixing each operand in `rip` (in order):
// rel32 ones get a new disp32; rel8 jumps (next == disp + 1: jcc 70-7f, jmp eb) are rewritten as rel32 ones
// (0f 80+cc / e9), so the copy can be longer. A branch back into the copied bytes can't be moved and is refused.
size_t hook_relocate(const uint8_t *code, size_t len, const HookRip *rip, size_t nrip, uintptr_t from, uint8_t *out,
                     size_t cap, uintptr_t to) {
    size_t in = 0, n = 0; // read and write positions
    for (size_t i = 0; i < nrip; i++) {
        size_t disp = rip[i].disp, next = rip[i].next;
        if (disp < in || disp < 1 || next > len)
            return 0;
        bool rel8 = next == disp + 1;
        if (!rel8 && disp + 4 > next)
            return 0;
        int64_t target = (int64_t)(from + next) + (rel8 ? (int8_t)code[disp] : 0);
        if (!rel8) {
            int32_t d;
            memcpy(&d, code + disp, 4);
            target += d;
        }
        if (target >= (int64_t)from && target < (int64_t)(from + len))
            return 0; // into the moved bytes
        if (rel8) {
            uint8_t op = code[disp - 1];
            if (!((op >= 0x70 && op <= 0x7f) || op == 0xeb))
                return 0; // jrcxz / loop have no rel32 form
            size_t head = disp - 1 - in, grown = op == 0xeb ? 5 : 6;
            if (n + head + grown > cap)
                return 0;
            memcpy(out + n, code + in, head);
            n += head;
            if (op == 0xeb)
                out[n++] = 0xe9;
            else {
                out[n++] = 0x0f;
                out[n++] = (uint8_t)(0x80 | (op & 0x0f));
            }
            n += 4;
        } else {
            if (n + next - in > cap)
                return 0;
            memcpy(out + n, code + in, next - in);
            n += next - in;
        }
        int64_t moved = target - (int64_t)(to + n); // relative to the end of the new instruction
        if (moved != (int32_t)moved)
            return 0;
        int32_t d = (int32_t)moved;
        memcpy(out + n - (rel8 ? 4 : next - disp), &d, 4);
        in = next;
    }
    if (n + len - in > cap)
        return 0;
    memcpy(out + n, code + in, len - in);
    return n + len - in;
}
