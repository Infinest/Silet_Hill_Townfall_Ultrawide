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
// A hook on FMinimalViewInfo::CalculateProjectionMatrixGivenView (hooks.cpp)
// complements the patch: cameras with bConstrainAspectRatio would otherwise
// get a projection matrix built for the authored aspect property and the
// image would stretch across the full 32:9 viewport.
//
// Configuration (TownfallUltraWide.ini, next to this DLL):
//   [Camera] Enabled  0/1 - master switch for the 32:9 gameplay fix
//   [UI]     Constrain 0/1/2 - off / 16:9 HUD box / 21:9 HUD box (gameplay only)
// The ini is generated with defaults on first launch if it does not exist.

#include <windows.h>

#include <cstring>

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

void InstallCameraHooks(HMODULE game);
void InstallUIConstraint(HMODULE game);

static void IniPath(wchar_t* path) {
    HMODULE self;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)&IniPath, &self)) {
        path[0] = L'\0';
        return;
    }
    GetModuleFileNameW(self, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"TownfallUltraWide.ini");
}

// Write a default ini with explanations if the user has none yet.
static void EnsureDefaultIni() {
    wchar_t path[MAX_PATH];
    IniPath(path);
    if (!path[0] || GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return;

    static const char kDefaultIni[] =
        "; Townfall UltraWide mod - configuration\r\n"
        "; Generated automatically. Delete this file to reset to defaults.\r\n"
        "\r\n"
        "[Camera]\r\n"
        "; 0 - off, 1 - on\r\n"
        "; Unlocks super ultrawide (32:9) gameplay by removing the 21:9 aspect\r\n"
        "; cap and pillarboxing. Vertical FOV stays as authored for 16:9, so\r\n"
        "; the image is never stretched.\r\n"
        "Enabled=1\r\n"
        "\r\n"
        "[UI]\r\n"
        "; Constrain: 0 - off, the HUD spans the full screen width\r\n"
        ";            1 - constrain the in-game HUD to a centered 16:9 box\r\n"
        ";            2 - constrain the in-game HUD to a centered 21:9 box\r\n"
        "; Applies only during gameplay. Main menu, inventory and pause\r\n"
        "; screens always use the full screen width.\r\n"
        "Constrain=0\r\n"
        "\r\n"
        "[Log]\r\n"
        "; 0 - off, 1 - on\r\n"
        "; Writes TownfallUltraWide.log next to the DLL. Only useful for\r\n"
        "; troubleshooting; keep off during normal play.\r\n"
        "Enabled=0\r\n";

    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, kDefaultIni, sizeof(kDefaultIni) - 1, &written, nullptr);
    CloseHandle(f);
    LogLine("default TownfallUltraWide.ini created");
}

static bool CameraEnabled() {
    wchar_t path[MAX_PATH];
    IniPath(path);
    if (!path[0]) return true;
    return GetPrivateProfileIntW(L"Camera", L"Enabled", 1, path) != 0;
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

    if (!CameraEnabled()) {
        LogLine("camera fix disabled via TownfallUltraWide.ini");
        return 0;
    }

    // The projection hook must be in place before the first constrained
    // camera renders, so install it before applying the byte patch.
    InstallCameraHooks(game);

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
        EnsureDefaultIni();
        HANDLE h = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
