// ddxlaunch.exe: start Darkest Dungeon (Windows, 64-bit) with ddx.dll loaded; the Windows run_game.sh.
//   ddxlaunch.exe [--game <install folder>] [game arguments...]
//   ddxlaunch.exe <path to Darkest.exe> [game arguments...]     (Steam launch option: "<path>\ddxlaunch.exe" %command%)
// ddx.dll must be next to ddxlaunch.exe. The game folder is --game, else the launcher's own folder if it holds
// _windows\win64\Darkest.exe, else Steam's (HKCU\Software\Valve\Steam\SteamPath)\steamapps\common\DarkestDungeon.
// Steam must be running (the game's Steam API needs it).
//
// The game starts suspended in its install folder (it looks for its 'pin' file there), with SteamAppId/SteamGameId
// set so the Steam API doesn't relaunch it through Steam. Its entry point is then pointed at a stub that loads
// ddx.dll, calls ddx_init (which installs the hooks), puts the entry point's bytes back and jumps to it:
// the hooks are in before any game code runs, and no loader lock is held while they install.
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef LONG(WINAPI *NtQIP)(HANDLE, int, void *, ULONG, ULONG *);
typedef struct {
    void *reserved1;
    void *peb;
    void *reserved2[2];
    ULONG_PTR pid;
    void *reserved3;
} ProcessBasicInfo;

static void fail(const wchar_t *what) {
    DWORD e = GetLastError();
    fwprintf(stderr, L"ddxlaunch: %ls (error %lu)\n", what, (unsigned long)e);
    MessageBoxW(NULL, what, L"ddxlaunch", MB_ICONERROR);
    ExitProcess(1);
}

static bool file_exists(const wchar_t *p) {
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

static bool game_dir_from_steam(wchar_t *out, size_t n) {
    HKEY k;
    wchar_t steam[MAX_PATH];
    DWORD size = sizeof steam, type;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &k) != ERROR_SUCCESS)
        return false;
    LONG r = RegQueryValueExW(k, L"SteamPath", NULL, &type, (BYTE *)steam, &size);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || type != REG_SZ)
        return false;
    for (wchar_t *c = steam; *c; c++)
        if (*c == L'/')
            *c = L'\\';
    swprintf(out, n, L"%ls\\steamapps\\common\\DarkestDungeon", steam);
    return true;
}

int wmain(int argc, wchar_t **argv) {
    wchar_t here[MAX_PATH], dll[MAX_PATH], game[MAX_PATH] = L"", exe[MAX_PATH];
    GetModuleFileNameW(NULL, here, MAX_PATH);
    *wcsrchr(here, L'\\') = 0;
    swprintf(dll, MAX_PATH, L"%ls\\ddx.dll", here);
    if (!file_exists(dll))
        fail(L"ddx.dll not found next to ddxlaunch.exe");

    int first = 1;
    if (argc > 2 && !wcscmp(argv[1], L"--game")) {
        wcsncpy(game, argv[2], MAX_PATH - 1);
        first = 3;
    } else if (argc > 1) { // Steam's %command%: the game's exe, then its arguments
        size_t l = wcslen(argv[1]);
        if (l > 11 && !_wcsicmp(argv[1] + l - 11, L"Darkest.exe")) {
            wchar_t full[MAX_PATH];
            if (!GetFullPathNameW(argv[1], MAX_PATH, full, NULL))
                fail(L"bad game path");
            for (wchar_t *c = full; *c; c++)
                if (*c == L'/')
                    *c = L'\\';
            for (int up = 0; up < 3; up++) {
                wchar_t *sl = wcsrchr(full, L'\\');
                if (sl)
                    *sl = 0;
            } // _windows\win64\Darkest.exe
            wcscpy(game, full);
            first = 2;
        }
    }
    if (!game[0]) {
        swprintf(exe, MAX_PATH, L"%ls\\_windows\\win64\\Darkest.exe", here);
        if (file_exists(exe))
            wcscpy(game, here);
    }
    if (!game[0] && !game_dir_from_steam(game, MAX_PATH))
        fail(L"game folder not found (use --game <folder>)");
    swprintf(exe, MAX_PATH, L"%ls\\_windows\\win64\\Darkest.exe", game);
    if (!file_exists(exe))
        fail(L"_windows\\win64\\Darkest.exe not found in the game folder (use --game <folder>)");

    // command line: "Darkest.exe" args...
    static wchar_t cmd[32768];
    swprintf(cmd, 32768, L"\"%ls\"", exe);
    for (int i = first; i < argc; i++) {
        wcscat(cmd, L" \"");
        wcsncat(cmd, argv[i], 32768 - wcslen(cmd) - 2);
        wcscat(cmd, L"\"");
    }
    SetEnvironmentVariableW(L"SteamAppId", L"262060");
    SetEnvironmentVariableW(L"SteamGameId", L"262060");

    STARTUPINFOW si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, game, &si, &pi))
        fail(L"CreateProcess failed");

    // the game's entry point: PEB -> image base -> PE header
    NtQIP qip = (NtQIP)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");
    ProcessBasicInfo pbi;
    uint64_t image = 0;
    SIZE_T got;
    if (!qip || qip(pi.hProcess, 0, &pbi, sizeof pbi, NULL) != 0 ||
        !ReadProcessMemory(pi.hProcess, (uint8_t *)pbi.peb + 0x10, &image, 8, &got) || !image) {
        TerminateProcess(pi.hProcess, 1);
        fail(L"can't find the game's image");
    }
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    if (!ReadProcessMemory(pi.hProcess, (void *)image, &dos, sizeof dos, &got) ||
        !ReadProcessMemory(pi.hProcess, (void *)(image + dos.e_lfanew), &nt, sizeof nt, &got) ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        TerminateProcess(pi.hProcess, 1);
        fail(L"the game isn't a 64-bit executable");
    }
    uint64_t entry = image + nt.OptionalHeader.AddressOfEntryPoint;

    // the stub, with its data after the code
    uint8_t saved[12];
    if (!ReadProcessMemory(pi.hProcess, (void *)entry, saved, sizeof saved, &got)) {
        TerminateProcess(pi.hProcess, 1);
        fail(L"can't read the entry point");
    }
    uint8_t *remote = VirtualAllocEx(pi.hProcess, NULL, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!remote) {
        TerminateProcess(pi.hProcess, 1);
        fail(L"VirtualAllocEx failed");
    }
    uint64_t load = (uint64_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    uint64_t proc = (uint64_t)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetProcAddress");
    uint8_t code[0x1000];
    memset(code, 0xCC, sizeof code);
    size_t n = 0;
#define B(...)                                                                                                         \
    do {                                                                                                               \
        const uint8_t b_[] = {__VA_ARGS__};                                                                            \
        memcpy(code + n, b_, sizeof b_);                                                                               \
        n += sizeof b_;                                                                                                \
    } while (0)
#define Q(v)                                                                                                           \
    do {                                                                                                               \
        uint64_t q_ = (v);                                                                                             \
        memcpy(code + n, &q_, 8);                                                                                      \
        n += 8;                                                                                                        \
    } while (0)
    const size_t DATA = 0x200, PATH = DATA, NAME = DATA + 0x400, SAVED = DATA + 0x420;
    B(0x51, 0x52);             // push rcx ; push rdx (the entry point's arguments)
    B(0x48, 0x83, 0xEC, 0x28); // sub rsp, 0x28
    B(0x48, 0xB9);
    Q((uint64_t)remote + PATH); // mov rcx, dll path
    B(0x48, 0xB8);
    Q(load);             // mov rax, LoadLibraryW
    B(0xFF, 0xD0);       // call rax
    B(0x48, 0x85, 0xC0); // test rax, rax
    size_t jz1 = n;
    B(0x74, 0x00);       // jz done
    B(0x48, 0x89, 0xC1); // mov rcx, rax
    B(0x48, 0xBA);
    Q((uint64_t)remote + NAME); // mov rdx, "ddx_init"
    B(0x48, 0xB8);
    Q(proc);             // mov rax, GetProcAddress
    B(0xFF, 0xD0);       // call rax
    B(0x48, 0x85, 0xC0); // test rax, rax
    size_t jz2 = n;
    B(0x74, 0x00); // jz done
    B(0xFF, 0xD0); // call rax (ddx_init)
    size_t done = n;
    code[jz1 + 1] = (uint8_t)(done - (jz1 + 2));
    code[jz2 + 1] = (uint8_t)(done - (jz2 + 2));
    B(0x49, 0x89, 0xF2, 0x49, 0x89, 0xFB); // mov r10, rsi ; mov r11, rdi
    B(0x48, 0xBE);
    Q((uint64_t)remote + SAVED); // mov rsi, saved bytes
    B(0x48, 0xBF);
    Q(entry);                                // mov rdi, entry
    B(0xB9, sizeof saved, 0x00, 0x00, 0x00); // mov ecx, 12
    B(0xF3, 0xA4);                           // rep movsb (entry point restored)
    B(0x4C, 0x89, 0xD6, 0x4C, 0x89, 0xDF);   // mov rsi, r10 ; mov rdi, r11
    B(0x48, 0x83, 0xC4, 0x28);               // add rsp, 0x28
    B(0x5A, 0x59);                           // pop rdx ; pop rcx
    B(0x48, 0xB8);
    Q(entry);      // mov rax, entry
    B(0xFF, 0xE0); // jmp rax
    wcscpy((wchar_t *)(code + PATH), dll);
    strcpy((char *)(code + NAME), "ddx_init");
    memcpy(code + SAVED, saved, sizeof saved);
    if (!WriteProcessMemory(pi.hProcess, remote, code, sizeof code, &got)) {
        TerminateProcess(pi.hProcess, 1);
        fail(L"can't write the stub");
    }

    // entry point -> stub: mov rax, stub ; jmp rax (12 bytes); the stub writes the originals back
    uint8_t jump[12] = {0x48, 0xB8};
    uint64_t stub = (uint64_t)remote;
    memcpy(jump + 2, &stub, 8);
    jump[10] = 0xFF;
    jump[11] = 0xE0;
    DWORD old;
    if (!VirtualProtectEx(pi.hProcess, (void *)entry, sizeof jump, PAGE_EXECUTE_READWRITE, &old) ||
        !WriteProcessMemory(pi.hProcess, (void *)entry, jump, sizeof jump, &got)) {
        TerminateProcess(pi.hProcess, 1);
        fail(L"can't patch the entry point");
    }
    FlushInstructionCache(pi.hProcess, (void *)entry, sizeof jump);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE); // stay until the game quits: Steam counts the game as running
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    return (int)exit_code;
}
