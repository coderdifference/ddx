// Offline test for compare sites (hook_cmp.c, install_cmp_site in hook.c), built for x86_64 and run (Rosetta on Apple
// silicon): small functions with a `cmp r32, [rip+disp32]` are written to memory, patched, and called.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "../src/common.h"

bool g_debug;
void logf_(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

static int g_failures;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x);                                                         \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

static void *g_seen_ud;
static unsigned g_seen_value, g_seen_operand;
static bool equal_or_99(void *ud, unsigned value, unsigned operand) {
    g_seen_ud = ud;
    g_seen_value = value;
    g_seen_operand = operand;
    return value == operand || value == 99;
}

// A function int f(int x): mov edx, 0x1234; <load x into the register>; cmp reg, [rip+K] (K = 42); sete al;
// movzx eax, al; shl edx, 8; or eax, edx; <restore>; ret. Returns ZF | edx << 8, so edx surviving the stub shows too.
typedef int (*TestFn)(int);
static TestFn build(uint8_t *page, const uint8_t *load, size_t nload, const uint8_t *cmp, size_t ncmp,
                    const uint8_t *restore, size_t nrestore, uint8_t **site) {
    uint8_t *p = page;
    static const uint8_t edx[] = {0xBA, 0x34, 0x12, 0, 0};
    memcpy(p, edx, sizeof edx);
    p += sizeof edx;
    memcpy(p, load, nload);
    p += nload;
    *site = p;
    memcpy(p, cmp, ncmp);
    int32_t disp = (int32_t)((page + 0x800) - (p + ncmp)); // the constant at page+0x800
    memcpy(p + ncmp - 4, &disp, 4);
    p += ncmp;
    static const uint8_t tail[] = {0x0F, 0x94, 0xC0, 0x0F, 0xB6, 0xC0, 0xC1, 0xE2, 0x08, 0x09, 0xD0};
    memcpy(p, tail, sizeof tail);
    p += sizeof tail;
    memcpy(p, restore, nrestore);
    p += nrestore;
    *p = 0xC3;
    *(unsigned *)(page + 0x800) = 42;
    return (TestFn)page;
}

static void run(const char *what, const uint8_t *load, size_t nload, const uint8_t *cmp, size_t ncmp,
                const uint8_t *restore, size_t nrestore) {
    uint8_t *page = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    uint8_t *site;
    TestFn f = build(page, load, nload, cmp, ncmp, restore, nrestore, &site);
    CHECK(f(42) == (1 | 0x1234 << 8) && f(99) == 0x1234 << 8); // before: plain cmp
    uint8_t expected[8];
    memcpy(expected, site, ncmp);
    int ud;
    CHECK(install_cmp_site(what, site, expected, ncmp, equal_or_99, &ud));
    CHECK(f(42) == (1 | 0x1234 << 8)); // the vanilla value still equal
    CHECK(g_seen_ud == &ud && g_seen_value == 42 && g_seen_operand == 42);
    CHECK(f(99) == (1 | 0x1234 << 8)); // the callback's extra value
    CHECK(f(7) == 0x1234 << 8 && g_seen_value == 7);
    CHECK(!install_cmp_site(what, site, expected, ncmp, equal_or_99, &ud)); // twice: the bytes changed
}

// Win64 stubs (hook_cmp_stub_win64, installed by hook_win.c): the callback has the MS ABI; the stub goes in the same
// page (rel32 reach) and the site becomes a jmp to it, as hook_win.c patches it.
__attribute__((ms_abi)) static bool equal_or_99_ms(void *ud, unsigned value, unsigned operand) {
    return equal_or_99(ud, value, operand);
}

static bool patch_win(uint8_t *page, uint8_t *site, size_t len, void *ud) {
    uint8_t insn[16], *stub = page + 0x400;
    memcpy(insn, site, len);
    if (!hook_cmp_stub_win64(stub, 0x300, insn, len, (uintptr_t)site, (uintptr_t)stub, (HookCmpFn)equal_or_99_ms, ud))
        return false;
    int32_t rel = (int32_t)(stub - (site + 5));
    site[0] = 0xE9;
    memcpy(site + 1, &rel, 4);
    memset(site + 5, 0x90, len - 5);
    return true;
}

static void run_win(const char *what, const uint8_t *load, size_t nload, const uint8_t *cmp, size_t ncmp,
                    const uint8_t *restore, size_t nrestore) {
    uint8_t *page = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    uint8_t *site;
    TestFn f = build(page, load, nload, cmp, ncmp, restore, nrestore, &site);
    int ud;
    CHECK(patch_win(page, site, ncmp, &ud));
    CHECK(f(42) == (1 | 0x1234 << 8));
    CHECK(g_seen_ud == &ud && g_seen_value == 42 && g_seen_operand == 42);
    CHECK(f(99) == (1 | 0x1234 << 8));
    CHECK(f(7) == 0x1234 << 8 && g_seen_value == 7);
    printf("win64 %s\n", what);
}

// The pair form: int f(int x, int *buf) with buf[4] = x: mov edx, 0x1234; <setup>; mov r32, [rip+K];
// cmp [base+0x10], r32; sete cl; movzx ecx, cl; shl eax, 8; or eax, ecx; <restore>; ret. Returns ZF | eax << 8
// (eax: the constant when it is the pair's register, so the relocated mov shows).
typedef int (*PairFn)(int, int *);
static void run_pair(const char *what, const uint8_t *setup, size_t nsetup, const uint8_t *site_bytes, size_t nsite,
                     const uint8_t *restore, size_t nrestore, bool eax) {
    uint8_t *page = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0), *p = page;
    static const uint8_t head[] = {0xBA, 0x34, 0x12, 0, 0, 0x89, 0x7E, 0x10}; // mov edx, 0x1234; mov [rsi+0x10], edi
    memcpy(p, head, sizeof head);
    p += sizeof head;
    memcpy(p, setup, nsetup);
    p += nsetup;
    uint8_t *site = p;
    memcpy(p, site_bytes, nsite);
    size_t mov_len = site_bytes[0] == 0x8B ? 6 : 7;
    int32_t disp = (int32_t)((page + 0x800) - (p + mov_len));
    memcpy(p + mov_len - 4, &disp, 4);
    p += nsite;
    static const uint8_t tail[] = {0x0F, 0x94, 0xC1, 0x0F, 0xB6, 0xC9, 0xC1, 0xE0, 0x08, 0x09, 0xC8};
    memcpy(p, tail, sizeof tail);
    p += sizeof tail;
    memcpy(p, restore, nrestore);
    p += nrestore;
    *p = 0xC3;
    *(unsigned *)(page + 0x800) = 42;
    PairFn f = (PairFn)page;
    int buf[8], ud;
    CHECK((f(42, buf) & 1) == 1 && (f(99, buf) & 1) == 0); // before: plain mov + cmp
    CHECK(patch_win(page, site, nsite, &ud));
    CHECK((f(42, buf) & 1) == 1 && g_seen_ud == &ud && g_seen_value == 42 && g_seen_operand == 42);
    CHECK((f(99, buf) & 1) == 1);
    CHECK((f(7, buf) & 1) == 0 && g_seen_value == 7);
    if (eax)
        CHECK(f(7, buf) >> 8 == 42); // the register holds the constant
    printf("win64 pair %s\n", what);
}

int main(void) {
    // the register decoder
    CHECK(hook_cmp_register((const uint8_t[]){0x3B, 0x0D, 0, 0, 0, 0}, 6) == 1);        // ecx
    CHECK(hook_cmp_register((const uint8_t[]){0x3B, 0x05, 0, 0, 0, 0}, 6) == 0);        // eax
    CHECK(hook_cmp_register((const uint8_t[]){0x44, 0x3B, 0x05, 0, 0, 0, 0}, 7) == 8);  // r8d
    CHECK(hook_cmp_register((const uint8_t[]){0x3B, 0x25, 0, 0, 0, 0}, 6) == -1);       // esp
    CHECK(hook_cmp_register((const uint8_t[]){0x3B, 0x48, 0x40, 0, 0, 0}, 6) == -1);    // not rip-relative
    CHECK(hook_cmp_register((const uint8_t[]){0x48, 0x3B, 0x05, 0, 0, 0, 0}, 7) == -1); // 64-bit
    CHECK(hook_cmp_register((const uint8_t[]){0x39, 0x05, 0, 0, 0, 0}, 6) == -1);       // cmp r/m, r
    // patched and run: ecx, eax, r8d, ebx (saved by the stub itself), esi and edi (the callback's own arguments)
    run("ecx", (const uint8_t[]){0x89, 0xF9}, 2, (const uint8_t[]){0x3B, 0x0D, 0, 0, 0, 0}, 6, NULL, 0);
    run("eax", (const uint8_t[]){0x89, 0xF8}, 2, (const uint8_t[]){0x3B, 0x05, 0, 0, 0, 0}, 6, NULL, 0);
    run("r8d", (const uint8_t[]){0x41, 0x89, 0xF8}, 3, (const uint8_t[]){0x44, 0x3B, 0x05, 0, 0, 0, 0}, 7, NULL, 0);
    run("ebx", (const uint8_t[]){0x53, 0x89, 0xFB}, 3, (const uint8_t[]){0x3B, 0x1D, 0, 0, 0, 0}, 6,
        (const uint8_t[]){0x5B}, 1);
    run("esi", (const uint8_t[]){0x89, 0xFE}, 2, (const uint8_t[]){0x3B, 0x35, 0, 0, 0, 0}, 6, NULL, 0);
    run("edi", NULL, 0, (const uint8_t[]){0x3B, 0x3D, 0, 0, 0, 0}, 6, NULL, 0);
    // the pair decoder
    int base;
    int8_t d8;
    CHECK(hook_cmp_pair((const uint8_t[]){0x8B, 0x05, 0, 0, 0, 0, 0x41, 0x39, 0x42, 0x40}, 10, &base, &d8) == 0 &&
          base == 10 && d8 == 0x40); // eax, [r10+0x40]
    CHECK(hook_cmp_pair((const uint8_t[]){0x8B, 0x05, 0, 0, 0, 0, 0x39, 0x43, 0x44}, 9, &base, &d8) == 0 && base == 3);
    CHECK(hook_cmp_pair((const uint8_t[]){0x44, 0x8B, 0x05, 0, 0, 0, 0, 0x44, 0x39, 0x46, 0x10}, 11, &base, &d8) == 8);
    CHECK(hook_cmp_pair((const uint8_t[]){0x8B, 0x05, 0, 0, 0, 0, 0x39, 0x4B, 0x44}, 9, &base, &d8) ==
          -1); // ecx != eax
    CHECK(hook_cmp_pair((const uint8_t[]){0x8B, 0x05, 0, 0, 0, 0, 0x39, 0x44, 0x24}, 9, &base, &d8) == -1); // SIB
    CHECK(hook_cmp_pair((const uint8_t[]){0x8B, 0x05, 0, 0, 0, 0, 0x39, 0x40, 0x10}, 9, &base, &d8) == -1); // [rax]
    // Win64 stubs, both forms
    run_win("ecx", (const uint8_t[]){0x89, 0xF9}, 2, (const uint8_t[]){0x3B, 0x0D, 0, 0, 0, 0}, 6, NULL, 0);
    run_win("r8d", (const uint8_t[]){0x41, 0x89, 0xF8}, 3, (const uint8_t[]){0x44, 0x3B, 0x05, 0, 0, 0, 0}, 7, NULL, 0);
    run_win("ebx", (const uint8_t[]){0x53, 0x89, 0xFB}, 3, (const uint8_t[]){0x3B, 0x1D, 0, 0, 0, 0}, 6,
            (const uint8_t[]){0x5B}, 1);
    run_pair("eax, [rsi]", NULL, 0, (const uint8_t[]){0x8B, 0x05, 0, 0, 0, 0, 0x39, 0x46, 0x10}, 9, NULL, 0, true);
    run_pair("eax, [r10]", (const uint8_t[]){0x49, 0x89, 0xF2}, 3,
             (const uint8_t[]){0x8B, 0x05, 0, 0, 0, 0, 0x41, 0x39, 0x42, 0x10}, 10, NULL, 0, true);
    run_pair("eax, [rbx]", (const uint8_t[]){0x53, 0x48, 0x89, 0xF3}, 4,
             (const uint8_t[]){0x8B, 0x05, 0, 0, 0, 0, 0x39, 0x43, 0x10}, 9, (const uint8_t[]){0x5B}, 1, true);
    run_pair("r8d, [rsi]", NULL, 0, (const uint8_t[]){0x44, 0x8B, 0x05, 0, 0, 0, 0, 0x44, 0x39, 0x46, 0x10}, 11, NULL,
             0, false);
    printf(g_failures ? "%d failure(s)\n" : "all passed\n", g_failures);
    return g_failures != 0;
}
