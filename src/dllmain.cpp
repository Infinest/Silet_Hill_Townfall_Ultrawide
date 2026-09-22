// dllmain.cpp - Townfall Super Ultrawide (32:9) gameplay fix
//
// The game renders gameplay views through FViewport::CalculateViewExtents
// (RVA 0x1798174 in Townfall-Win64-Shipping.exe, UE 5.6.1), which letterboxes /
// pillarboxes the view rect when the camera's aspect-ratio constraint binds
// (bConstrainAspectRatio, e.g. the 21:9 gameplay clamp).
//
// This DLL is dropped into Townfall\Binaries\Win64 as dxgi.dll (Windows loads
// it for the D3D12 renderer) and patches one conditional jump in that function
// so the constraint math always takes the "leave the rect unchanged" path.
// Result: the full 32:9 viewport is used during gameplay.
//
// All offsets are located at runtime by pattern scanning, so game updates that
// move code will not corrupt anything - the patch simply refuses to apply.
//
// This build also installs temporary diagnostic hooks (hooks.cpp) that log
// camera FOV, aspect-ratio properties, view rects and the resulting projection
// matrix to TownfallUltraWide.log. They are installed before the patch is
// applied so early (pre-patch) frames are captured as well.

#include <windows.h>

#include "log.h"
#include "patch.h"

// First-chance logger for fatal exceptions: records code + RIP (module
// offset) so crashes in the game can be mapped to the exact instruction
// without needing a debugger, then lets normal handling continue.
static LONG WINAPI VectoredExceptionLogger(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
        code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_STACK_OVERFLOW) {
        HMODULE game = GetModuleHandleW(nullptr);
        uintptr_t base = reinterpret_cast<uintptr_t>(game);
        uintptr_t rip = ep->ContextRecord->Rip;
        HMODULE mod = nullptr;
        char modName[64] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(rip), &mod)) {
            GetModuleFileNameA(mod, modName, sizeof(modName));
            const char* slash = strrchr(modName, '\\');
            if (slash) memmove(modName, slash + 1, strlen(slash));
        }
        LogLine("CRASH code=%X rip=%llX mod=%s modoff=%llX addr=%llX", code,
                (unsigned long long)rip, modName,
                (unsigned long long)(rip - reinterpret_cast<uintptr_t>(mod)),
                (unsigned long long)(uintptr_t)ep->ExceptionRecord->ExceptionInformation[1]);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void InstallDiagnosticHooks(HMODULE game);
void InstallUIConstraint(HMODULE game);

static bool ConfigEnabled() {
    wchar_t path[MAX_PATH];
    HMODULE self;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)&ConfigEnabled, &self)) {
        GetModuleFileNameW(self, path, MAX_PATH);
        wchar_t* slash = wcsrchr(path, L'\\');
        if (slash) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"TownfallUltraWide.ini");
    } else {
        return true;
    }
    return GetPrivateProfileIntW(L"Patch", L"Enabled", 1, path) != 0;
}

static bool HookEnabled(LPCWSTR name) {
    wchar_t path[MAX_PATH];
    HMODULE self;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)&HookEnabled, &self)) {
        return true;
    }
    GetModuleFileNameW(self, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"TownfallUltraWide.ini");
    return GetPrivateProfileIntW(L"Hooks", name, 1, path) != 0;
}

static DWORD WINAPI InitThread(LPVOID) {
    HMODULE game = GetModuleHandleW(nullptr);
    if (!game) {
        LogLine("ERROR: game module not found");
        return 1;
    }

    // UI constraint (step 2): hooked before the patches so the full chain is
    // captured early.
    InstallUIConstraint(game);

    // Diagnostic instrumentation first: captures pre-patch behavior too.
    InstallDiagnosticHooks(game);

    if (!ConfigEnabled()) {
        LogLine("Patch disabled via TownfallUltraWide.ini");
        return 0;
    }

    const PatchResult result = ApplyUltrawidePatch(game);
    switch (result) {
        case PatchResult::Applied:
            LogLine("OK: 32:9 gameplay patch applied (FViewport::CalculateViewExtents neutralized)");
            break;
        case PatchResult::PatternNotFound:
            LogLine("ERROR: pattern not found - game version changed? Patch NOT applied.");
            break;
        case PatchResult::PatternNotUnique:
            LogLine("ERROR: pattern not unique - refusing to patch.");
            break;
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
        // crash logger disabled: first-chance handler I/O can interfere with the game's own SEH-based probes
        LogLine("dxgi proxy attached (pid %lu)", GetCurrentProcessId());
        HANDLE h = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
