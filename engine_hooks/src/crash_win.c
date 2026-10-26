// Windows: a crash report in ddx.log. The first fatal exception (an access violation, illegal instruction or stack
// overflow, the game's or ours) logs its code, where it happened and the code addresses found on the stack, each as
// module+offset (Darkest.exe offsets are the RVAs syms_win.c uses), then lets the game handle it as before. A vectored
// handler, so it runs before the game's own crash handling. (macOS writes its own crash reports.)
#include "common.h"
#ifdef _WIN32
#include <windows.h>
#include <stdio.h>

static void where(uint64_t a, char *out, size_t n) {
    HMODULE m = NULL;
    char path[MAX_PATH] = "?";
    if (a &&
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (const char *)(uintptr_t)a, &m) &&
        m) {
        GetModuleFileNameA(m, path, sizeof path);
        const char *base = strrchr(path, '\\');
        snprintf(out, n, "%s+0x%llx", base ? base + 1 : path, (unsigned long long)(a - (uint64_t)(uintptr_t)m));
    } else {
        snprintf(out, n, "0x%llx", (unsigned long long)a);
    }
}

static bool is_code(uint64_t a) {
    MEMORY_BASIC_INFORMATION mi;
    if (!a || !VirtualQuery((const void *)(uintptr_t)a, &mi, sizeof mi) || mi.State != MEM_COMMIT)
        return false;
    if (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
        return false;
    return (mi.Protect & 0xff) == PAGE_EXECUTE || (mi.Protect & 0xff) == PAGE_EXECUTE_READ ||
           (mi.Protect & 0xff) == PAGE_EXECUTE_READWRITE || (mi.Protect & 0xff) == PAGE_EXECUTE_WRITECOPY;
}

static bool is_readable(const void *p) {
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return false;
    DWORD protection = mi.Protect & 0xff;
    return protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
           protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
           protection == PAGE_EXECUTE_WRITECOPY;
}

static LONG WINAPI on_crash(EXCEPTION_POINTERS *e) {
    static volatile LONG once;
    const EXCEPTION_RECORD *r = e->ExceptionRecord;
    DWORD code = r->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW)
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedExchange(&once, 1))
        return EXCEPTION_CONTINUE_SEARCH;
    const CONTEXT *c = e->ContextRecord;
    char at[300];
    where(c->Rip, at, sizeof at);
    logf_("CRASH: exception %08lx at %s (rip %p)%s", (unsigned long)r->ExceptionCode, at, (void *)(uintptr_t)c->Rip,
          r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2
              ? (r->ExceptionInformation[0] == 8 ? ", executing"
                 : r->ExceptionInformation[0]    ? ", writing"
                                                 : ", reading")
              : "");
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
        logf_("CRASH: address %p", (void *)r->ExceptionInformation[1]);
    logf_("CRASH: rax %llx rbx %llx rcx %llx rdx %llx rsi %llx rdi %llx r8 %llx r9 %llx rsp %llx", c->Rax, c->Rbx,
          c->Rcx, c->Rdx, c->Rsi, c->Rdi, c->R8, c->R9, c->Rsp);
    // the stack: code addresses (return addresses, mostly) in the first 512 slots, newest first
    const uint64_t *sp = (const uint64_t *)(uintptr_t)c->Rsp;
    int shown = 0;
    for (int i = 0; i < 512 && shown < 24; i++) {
        if (!is_readable(sp + i))
            break;
        if (!is_code(sp[i]))
            continue;
        where(sp[i], at, sizeof at);
        logf_("CRASH: stack [rsp+0x%x] %s", i * 8, at);
        shown++;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void crash_report_install(void) {
    AddVectoredExceptionHandler(1, on_crash);
}
#else
void crash_report_install(void) {}
#endif
