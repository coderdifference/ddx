// Inline hooks and game addresses, Windows (macOS: hook.c).
// The MSVC build of Darkest.exe has no symbols, so syms_win.c lists the addresses (found by matching the
// symbolled macOS binary) with the bytes expected there. A hook replaces whole instructions (>= 5 bytes) with
// a jmp rel32 to a relay near the image (the stubs pool below), which jumps on to our detour; the trampoline
// runs the replaced instructions and jumps back.
#include "common.h"
#ifdef _WIN32
#include <windows.h>
#include <string.h>

static uint8_t *image_base(void) {
    return (uint8_t *)GetModuleHandleA(NULL);
}

bool win_game_supported(void) {
    const uint8_t *b = image_base();
    if (!b) {
        logf_("Darkest.exe image is unavailable: hooks off");
        return false;
    }

    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)b;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        logf_("Darkest.exe has an invalid DOS header: hooks off");
        return false;
    }

    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(b + (size_t)dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
        logf_("Darkest.exe is not a valid 64-bit x86 PE image: hooks off");
        return false;
    }
    if (nt->FileHeader.TimeDateStamp == g_win_timestamp)
        return true;
    logf_("Darkest.exe build %08lx is not the one this library knows (%08x): hooks off",
          (unsigned long)nt->FileHeader.TimeDateStamp, g_win_timestamp);
    return false;
}

static const WinSym *entry_by_name(const char *name) {
    for (int i = 0; i < g_win_nsyms; i++)
        if (!strcmp(g_win_syms[i].name, name))
            return &g_win_syms[i];
    return NULL;
}

static const WinSym *entry_by_address(const void *p) {
    for (int i = 0; i < g_win_nsyms; i++)
        if (!g_win_syms[i].native && g_win_syms[i].rva && image_base() + g_win_syms[i].rva == p)
            return &g_win_syms[i];
    return NULL;
}

static bool hooked_by_us(const uint8_t *p);

// The game's DLLs export their C APIs by name (FMOD_*, SDL_*), as the macOS binary links them.
static void *dll_export(const char *name) {
    static const char *const dlls[] = {"fmod.dll", "fmodstudio.dll", "SDL2.dll"};
    for (size_t i = 0; i < sizeof dlls / sizeof dlls[0]; i++) {
        HMODULE h = GetModuleHandleA(dlls[i]);
        void *p = h ? (void *)GetProcAddress(h, name) : NULL;
        if (p)
            return p;
    }
    return NULL;
}

void *sym(const char *mangled) {
    const WinSym *e = entry_by_name(mangled);
    if (!e) {
        void *p = dll_export(mangled);
        if (!p)
            logf_("missing symbol %s (not in the Windows table)", mangled);
        return p;
    }
    if (e->native)
        return e->native;
    uint8_t *p = image_base() + e->rva;
    if (e->len && memcmp(p, e->bytes, e->len) != 0 && !hooked_by_us(p)) {
        logf_("%s: unexpected code at +%x (other game version?), unavailable", mangled, e->rva);
        return NULL;
    }
    return p;
}

// ---------------------------------------------------------------- stubs pool (within +-2 GB of the image)
static uint8_t *g_pool;
static size_t g_pool_used;
#define POOL_SIZE 0x10000

static uint8_t *pool_alloc(size_t n) {
    if (!g_pool) {
        uintptr_t start = ((uintptr_t)image_base() & ~(uintptr_t)0xffff) - POOL_SIZE;
        for (uintptr_t a = start; a > start - 0x70000000 && !g_pool; a -= 0x10000)
            g_pool = VirtualAlloc((void *)a, POOL_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (!g_pool) {
            logf_("hooks: no memory near the game's image");
            return NULL;
        }
    }
    n = (n + 15) & ~(size_t)15;
    if (g_pool_used + n > POOL_SIZE) {
        logf_("hooks: stubs pool full");
        return NULL;
    }
    uint8_t *p = g_pool + g_pool_used;
    g_pool_used += n;
    return p;
}

// p starts with our jmp rel32 into the stubs pool (a hook installed earlier: calling it runs the detour)
static bool hooked_by_us(const uint8_t *p) {
    if (!g_pool || p[0] != 0xE9)
        return false;
    int32_t rel;
    memcpy(&rel, p + 1, 4);
    const uint8_t *to = p + 5 + rel;
    return to >= g_pool && to < g_pool + POOL_SIZE;
}

static size_t put_abs_jmp(uint8_t *p, const void *to) { // jmp [rip+0]; dq to
    p[0] = 0xFF;
    p[1] = 0x25;
    memset(p + 2, 0, 4);
    uint64_t t = (uint64_t)to;
    memcpy(p + 6, &t, 8);
    return 14;
}

// Replace the entry's bytes at `at` with jmp rel32 -> relay.
static bool patch_jump(const char *name, uint8_t *at, size_t len, const uint8_t *relay) {
    int64_t rel = (int64_t)(relay - (at + 5));
    if (rel != (int32_t)rel) {
        logf_("%s: stub out of jump range", name);
        return false;
    }
    uint8_t patch[16];
    memset(patch, 0x90, sizeof patch);
    patch[0] = 0xE9;
    int32_t r32 = (int32_t)rel;
    memcpy(patch + 1, &r32, 4);
    DWORD old;
    if (!VirtualProtect(at, len, PAGE_EXECUTE_READWRITE, &old)) {
        logf_("%s: VirtualProtect failed", name);
        return false;
    }
    memcpy(at, patch, len);
    DWORD ignored;
    if (!VirtualProtect(at, len, old, &ignored))
        logf_("%s: couldn't restore code-page protection after patching", name);
    FlushInstructionCache(GetCurrentProcess(), at, len);
    return true;
}

// Copy the entry's bytes (from `from`) to `to` (room for COPY_MAX), fixing up its relative operand for the new place
// (a rel8 jump grows into a rel32 one). Returns the copy's length, 0 if it can't be moved.
#define COPY_MAX(e) ((size_t)(e)->len + 4)
static size_t copy_code(const char *name, const WinSym *e, const uint8_t *from, uint8_t *to) {
    const HookRip rip = {e->rip_disp, e->rip_next};
    size_t n =
        hook_relocate(e->bytes, e->len, &rip, e->rip_disp ? 1 : 0, (uintptr_t)from, to, COPY_MAX(e), (uintptr_t)to);
    if (!n)
        logf_("%s: relative operand can't be moved (out of range or into the copied bytes)", name);
    return n;
}

static const WinSym *checked_entry(const char *name, void *target) {
    if (!target) {
        logf_("%s: symbol not found, hook off", name);
        return NULL;
    }
    const WinSym *e = entry_by_address(target);
    if (!e || e->len < 5) {
        logf_("%s: no hookable bytes in the Windows table, hook off", name);
        return NULL;
    }
    if (hooked_by_us(target)) {
        logf_("%s: already hooked (one hook per function: share the first one), hook off", name);
        return NULL;
    }
    if (memcmp(target, e->bytes, e->len) != 0) {
        logf_("%s: unexpected prologue (other game version?), hook off", name);
        return NULL;
    }
    return e;
}

void *install_hook(const char *name, void *target, void *detour, const uint8_t *expected, size_t len) {
    (void)expected;
    (void)len; // the macOS prologue; the table has the Windows one
    const WinSym *e = checked_entry(name, target);
    if (!e)
        return NULL;
    uint8_t *tramp = pool_alloc(COPY_MAX(e) + 14), *relay = pool_alloc(14);
    if (!tramp || !relay)
        return NULL;
    size_t n = copy_code(name, e, target, tramp); // the original first instructions
    if (!n)
        return NULL;
    put_abs_jmp(tramp + n, (uint8_t *)target + e->len);
    put_abs_jmp(relay, detour);
    if (!patch_jump(name, target, e->len, relay))
        return NULL;
    logf_("%s: hooked at %p", name, target);
    return tramp;
}

void *install_hook_rip(const char *name, void *target, void *detour, const uint8_t *expected, size_t len,
                       const HookRip *rip, size_t nrip) {
    (void)rip;
    (void)nrip; // the macOS operands; the table entry has its own
    return install_hook(name, target, detour, expected, len);
}

void *install_hook_with_trampoline(const char *name, void *target, void *detour, const uint8_t *expected, size_t len,
                                   const uint8_t *tramp_code, size_t tramp_len) {
    (void)target;
    (void)detour;
    (void)expected;
    (void)len;
    (void)tramp_code;
    (void)tramp_len;
    logf_("%s: custom trampolines are macOS only, hook off", name);
    return NULL;
}

// Stub: save flags, the general registers (as a Regs on the stack) and xmm0-5, call fn(&regs) on an aligned
// stack, restore everything (the general registers from the Regs), run the replaced instructions, jump back after them.
static bool mid_hook(const char *name, void *site, void *fn) {
    const WinSym *e = checked_entry(name, site);
    if (!e)
        return false;
    static const uint8_t save[] = {
        0x9C,                                                                                           // pushfq
        0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x41, 0x53, 0x41, 0x52, 0x41, 0x51, 0x41, 0x50, // push r15..r8
        0x57, 0x56, 0x55, 0x53, 0x52, 0x51, 0x50,                               // push rdi rsi rbp rbx rdx rcx rax
        0x49, 0x89, 0xE4,                                                       // mov r12, rsp (the Regs)
        0x48, 0x83, 0xE4, 0xF0,                                                 // and rsp, -16
        0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00,                               // sub rsp, 0x80
        0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20, 0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30, // movdqu [rsp+0x20..], xmm0-5
        0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40, 0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50, 0xF3, 0x0F, 0x7F, 0x64,
        0x24, 0x60, 0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x70, 0x4C, 0x89, 0xE1, // mov rcx, r12
        0x48, 0xB8};                                                      // mov rax, imm64 (fn)
    static const uint8_t restore[] = {
        0xFF, 0xD0,                                                             // call rax
        0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20, 0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30, // movdqu xmm0-5, [rsp+0x20..]
        0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40, 0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50, 0xF3, 0x0F, 0x6F, 0x64,
        0x24, 0x60, 0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x70, 0x4C, 0x89, 0xE4, // mov rsp, r12
        0x58, 0x59, 0x5A, 0x5B, 0x5D, 0x5E, 0x5F,                         // pop rax rcx rdx rbx rbp rsi rdi
        0x41, 0x58, 0x41, 0x59, 0x41, 0x5A, 0x41, 0x5B, 0x41, 0x5C, 0x41, 0x5D, 0x41, 0x5E, 0x41, 0x5F, // pop r8..r15
        0x9D};                                                                                          // popfq
    uint8_t *stub = pool_alloc(sizeof save + 8 + sizeof restore + COPY_MAX(e) + 14);
    if (!stub)
        return false;
    uint8_t *p = stub;
    memcpy(p, save, sizeof save);
    p += sizeof save;
    uint64_t f = (uint64_t)fn;
    memcpy(p, &f, 8);
    p += 8;
    memcpy(p, restore, sizeof restore);
    p += sizeof restore;
    size_t n = copy_code(name, e, site, p); // the replaced instructions
    if (!n)
        return false;
    p += n;
    put_abs_jmp(p, (uint8_t *)site + e->len);
    if (!patch_jump(name, site, e->len, stub))
        return false;
    logf_("%s: hooked at %p", name, site);
    return true;
}

bool install_mid_hook(const char *name, void *site, void (*fn)(const Regs *regs)) {
    return mid_hook(name, site, (void *)fn);
}

bool install_mid_hook_rw(const char *name, void *site, void (*fn)(Regs *regs)) {
    return mid_hook(name, site, (void *)fn);
}

// Compare sites (common.h): the site is a table entry (its bytes the cmp, or MSVC's mov + cmp pair); the stub in the
// pool, then `jmp rel32 stub` (nop padded) over the bytes.
bool install_cmp_site(const char *name, void *site, const uint8_t *expected, size_t len, HookCmpFn fn, void *ud) {
    (void)expected;
    (void)len; // the macOS instruction; the table has the Windows one
    const WinSym *e = checked_entry(name, site);
    if (!e)
        return false;
    uint8_t *stub = pool_alloc(192);
    if (!stub)
        return false;
    if (!hook_cmp_stub_win64(stub, 192, e->bytes, e->len, (uintptr_t)site, (uintptr_t)stub, fn, ud)) {
        logf_("%s: not a supported compare (or out of reach), site off", name);
        return false;
    }
    if (!patch_jump(name, site, e->len, stub))
        return false;
    logf_("%s: compare site at %p", name, site);
    return true;
}
#endif
