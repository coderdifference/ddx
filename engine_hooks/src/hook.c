// Inline hooks (x86_64) and game symbol lookup, macOS (Windows: hook_win.c).
#include "common.h"
#ifndef _WIN32
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <string.h>
#include <sys/mman.h>

// A trampoline page: anywhere (mmap), or for code with a RIP-relative operand, within rel32 reach of `near`
// (the first free page above it, else below; the image's own pages are taken, so the search steps past them).
static uint8_t *tramp_page(const void *near) {
    if (!near) {
        void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        return p == MAP_FAILED ? NULL : p;
    }
    const mach_vm_address_t step = 0x100000, reach = 0x7ff00000; // 1 MB steps, a little under 2 GB
    mach_vm_address_t base = ((mach_vm_address_t)near + step) & ~(step - 1);
    for (mach_vm_address_t d = 0; d < reach; d += step) {
        mach_vm_address_t up = base + d, down = base - step - d;
        if (mach_vm_allocate(mach_task_self(), &up, 4096, VM_FLAGS_FIXED) == KERN_SUCCESS)
            return (uint8_t *)up;
        if (down > step && down < base &&
            mach_vm_allocate(mach_task_self(), &down, 4096, VM_FLAGS_FIXED) == KERN_SUCCESS)
            return (uint8_t *)down;
    }
    return NULL;
}

// Replace `target`'s first len bytes with jmp [rip+0] -> detour (nop padded).
static bool patch_target(const char *name, void *target, void *detour, size_t len) {
    uint8_t patch[14];
    patch[0] = 0xFF;
    patch[1] = 0x25;
    memset(patch + 2, 0, 4);
    uint64_t to = (uint64_t)detour;
    memcpy(patch + 6, &to, 8);

    mach_vm_address_t page = (mach_vm_address_t)target & ~((mach_vm_address_t)vm_page_size - 1);
    mach_vm_size_t size =
        ((mach_vm_address_t)target + len - page + vm_page_size - 1) & ~((mach_vm_size_t)vm_page_size - 1);
    kern_return_t kr =
        mach_vm_protect(mach_task_self(), page, size, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    if (kr != KERN_SUCCESS) {
        logf_("%s: vm_protect rw failed (%d)", name, kr);
        return false;
    }
    memcpy(target, patch, sizeof patch);
    memset((uint8_t *)target + sizeof patch, 0x90, len - sizeof patch); // nop padding
    kr = mach_vm_protect(mach_task_self(), page, size, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    if (kr != KERN_SUCCESS)
        logf_("%s: vm_protect rx failed (%d)", name, kr);
    logf_("%s: hooked at %p", name, target);
    return true;
}

static bool prologue_ok(const char *name, const void *target, const uint8_t *expected, size_t len) {
    if (!target) {
        logf_("%s: symbol not found, hook off", name);
        return false;
    }
    if (len < 14) {
        logf_("%s: a hook replaces at least 14 bytes (whole instructions), hook off", name);
        return false;
    }
    static const uint8_t ours[] = {0xFF, 0x25, 0, 0, 0, 0}; // patch_target's jmp [rip+0]
    if (memcmp(target, ours, sizeof ours) == 0) {
        logf_("%s: already hooked (one hook per function: share the first one), hook off", name);
        return false;
    }
    if (memcmp(target, expected, len) != 0) {
        logf_("%s: unexpected prologue (other game version?), hook off", name);
        return false;
    }
    return true;
}

void *install_hook_rip(const char *name, void *target, void *detour, const uint8_t *expected, size_t len,
                       const HookRip *rip, size_t nrip) {
    if (!prologue_ok(name, target, expected, len))
        return NULL;
    uint8_t *tramp = tramp_page(nrip ? target : NULL);
    if (!tramp) {
        logf_("%s: no trampoline memory%s, hook off", name, nrip ? " near the image" : "");
        return NULL;
    }
    // the original first instructions, their relative operands fixed for the new place
    size_t n = hook_relocate(expected, len, rip, nrip, (uintptr_t)target, tramp, 4096 - 14, (uintptr_t)tramp);
    if (!n) {
        logf_("%s: a relative operand can't be moved (misplaced, out of reach or into the replaced bytes), hook off",
              name);
        mach_vm_deallocate(mach_task_self(), (mach_vm_address_t)tramp, 4096);
        return NULL;
    }
    uint8_t *p = tramp + n;
    p[0] = 0xFF;
    p[1] = 0x25;
    memset(p + 2, 0, 4); // jmp [rip+0] -> rest of the original
    uint64_t back = (uint64_t)target + len;
    memcpy(p + 6, &back, 8);
    if (mprotect(tramp, 4096, PROT_READ | PROT_EXEC) != 0) {
        logf_("%s: mprotect failed", name);
        mach_vm_deallocate(mach_task_self(), (mach_vm_address_t)tramp, 4096);
        return NULL;
    }
    if (!patch_target(name, target, detour, len)) {
        mach_vm_deallocate(mach_task_self(), (mach_vm_address_t)tramp, 4096);
        return NULL;
    }
    return tramp;
}

void *install_hook(const char *name, void *target, void *detour, const uint8_t *expected, size_t len) {
    return install_hook_rip(name, target, detour, expected, len, NULL, 0);
}

// Like install_hook, but with a caller-built trampoline (for prologues that contain a relative jump, which
// can't be copied as is). `tramp` must end by jumping back into the original.
void *install_hook_with_trampoline(const char *name, void *target, void *detour, const uint8_t *expected, size_t len,
                                   const uint8_t *tramp_code, size_t tramp_len) {
    if (!prologue_ok(name, target, expected, len))
        return NULL;
    if (!tramp_code || !tramp_len || tramp_len > 4096) {
        logf_("%s: invalid custom trampoline size, hook off", name);
        return NULL;
    }
    uint8_t *tramp = tramp_page(NULL);
    if (!tramp) {
        logf_("%s: mmap failed", name);
        return NULL;
    }
    memcpy(tramp, tramp_code, tramp_len);
    if (mprotect(tramp, 4096, PROT_READ | PROT_EXEC) != 0) {
        logf_("%s: mprotect failed", name);
        munmap(tramp, 4096);
        return NULL;
    }
    if (!patch_target(name, target, detour, len)) {
        munmap(tramp, 4096);
        return NULL;
    }
    return tramp;
}

// A compare site (common.h): the stub on a page within rel32 reach, then `jmp rel32 stub` (nop padded) over the cmp.
bool install_cmp_site(const char *name, void *site, const uint8_t *expected, size_t len, HookCmpFn fn, void *ud) {
    if (!site) {
        logf_("%s: symbol not found, site off", name);
        return false;
    }
    if (hook_cmp_register(expected, len) < 0) {
        logf_("%s: not a cmp r32, [rip+disp32], site off", name);
        return false;
    }
    if (memcmp(site, expected, len) != 0) {
        logf_("%s: unexpected bytes (other game version, or already patched), site off", name);
        return false;
    }
    uint8_t *stub = tramp_page(site);
    if (!stub) {
        logf_("%s: no stub memory near the image, site off", name);
        return false;
    }
    if (!hook_cmp_stub(stub, 4096, expected, len, (uintptr_t)site, (uintptr_t)stub, fn, ud) ||
        mprotect(stub, 4096, PROT_READ | PROT_EXEC) != 0) {
        logf_("%s: couldn't build the stub, site off", name);
        mach_vm_deallocate(mach_task_self(), (mach_vm_address_t)stub, 4096);
        return false;
    }
    uint8_t patch[7];
    int32_t rel = (int32_t)((intptr_t)stub - ((intptr_t)site + 5));
    patch[0] = 0xE9;
    memcpy(patch + 1, &rel, 4);
    memset(patch + 5, 0x90, len - 5);
    mach_vm_address_t page = (mach_vm_address_t)site & ~((mach_vm_address_t)vm_page_size - 1);
    mach_vm_size_t size =
        ((mach_vm_address_t)site + len - page + vm_page_size - 1) & ~((mach_vm_size_t)vm_page_size - 1);
    kern_return_t kr =
        mach_vm_protect(mach_task_self(), page, size, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    if (kr != KERN_SUCCESS) {
        logf_("%s: vm_protect rw failed (%d), site off", name, kr);
        mach_vm_deallocate(mach_task_self(), (mach_vm_address_t)stub, 4096);
        return false;
    }
    memcpy(site, patch, len);
    kr = mach_vm_protect(mach_task_self(), page, size, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    if (kr != KERN_SUCCESS)
        logf_("%s: vm_protect rx failed (%d)", name, kr);
    logf_("%s: compare site at %p", name, site);
    return true;
}

// Private symbols (e.g. Hero::GetGuid, "was a private external") aren't exported, so dlsym can't see
// them; the executable's full symbol table (LC_SYMTAB, in __LINKEDIT) still names them.
static void *symtab_lookup(const char *mangled) {
    const struct mach_header_64 *mh = NULL;
    intptr_t slide = 0;
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const struct mach_header *h = _dyld_get_image_header(i);
        if (h && h->magic == MH_MAGIC_64 && h->filetype == MH_EXECUTE) {
            mh = (const struct mach_header_64 *)h;
            slide = _dyld_get_image_vmaddr_slide(i);
            break;
        }
    }
    if (!mh)
        return NULL;
    const struct segment_command_64 *linkedit = NULL;
    const struct symtab_command *st = NULL;
    const uint8_t *lc = (const uint8_t *)(mh + 1);
    size_t remaining = mh->sizeofcmds;
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        if (remaining < sizeof(struct load_command))
            return NULL;
        const struct load_command *c = (const struct load_command *)lc;
        if (c->cmdsize < sizeof(*c) || c->cmdsize > remaining)
            return NULL;
        if (c->cmd == LC_SEGMENT_64 && c->cmdsize >= sizeof(struct segment_command_64)) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)c;
            if (strcmp(seg->segname, "__LINKEDIT") == 0)
                linkedit = seg;
        } else if (c->cmd == LC_SYMTAB && c->cmdsize >= sizeof(struct symtab_command)) {
            st = (const struct symtab_command *)c;
        }
        lc += c->cmdsize;
        remaining -= c->cmdsize;
    }
    if (!linkedit || !st)
        return NULL;
    uint64_t linkedit_end = linkedit->fileoff + linkedit->filesize;
    uint64_t symbols_bytes = (uint64_t)st->nsyms * sizeof(struct nlist_64);
    if (linkedit_end < linkedit->fileoff || st->symoff < linkedit->fileoff ||
        (uint64_t)st->symoff + symbols_bytes > linkedit_end || st->stroff < linkedit->fileoff ||
        (uint64_t)st->stroff + st->strsize > linkedit_end)
        return NULL;
    const uint8_t *base = (const uint8_t *)(slide + linkedit->vmaddr - linkedit->fileoff);
    const struct nlist_64 *syms = (const struct nlist_64 *)(base + st->symoff);
    const char *strs = (const char *)(base + st->stroff);
    for (uint32_t i = 0; i < st->nsyms; i++) {
        if ((syms[i].n_type & N_TYPE) != N_SECT)
            continue;
        if (syms[i].n_un.n_strx >= st->strsize)
            continue;
        const char *n = strs + syms[i].n_un.n_strx;
        size_t left = st->strsize - syms[i].n_un.n_strx;
        if (n[0] == '_' && memchr(n, '\0', left) && strcmp(n + 1, mangled) == 0)
            return (void *)(syms[i].n_value + slide);
    }
    return NULL;
}

void *sym(const char *mangled) {
    void *p = dlsym(RTLD_DEFAULT, mangled);
    if (!p)
        p = symtab_lookup(mangled);
    if (!p)
        logf_("missing symbol %s", mangled);
    return p;
}
#endif
