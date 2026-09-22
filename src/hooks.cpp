// hooks.cpp - Diagnostic instrumentation for the 32:9 fix.
//
// Hooks three functions to learn exactly what the game does with FOV, the
// camera AspectRatio property, view rects and the projection matrix:
//
//   UCameraComponent::GetCameraView @ 0x169FD00
//       rcx = camera, xmm0 = dt, r8 = FMinimalViewInfo& out
//
//   FViewport::CalculateViewExtents (wrapper) @ 0x1798100
//       rcx = FIntRect& out (sret), rdx = FViewport, r9 = FIntRect& in
//
//   FMinimalViewInfo::CalculateProjectionMatrixGivenView @ 0x1798088
//       rcx = FMinimalViewInfo, dl = EAspectRatioAxisConstraint,
//       r8 = FViewport, r9 = FSceneViewInitOptions& (ProjectionMatrix @ +0xA0
//       as doubles, ViewRectangle @ +0x120)

#include <windows.h>

#include <cstdint>

#include "detour.h"
#include "log.h"

namespace {

bool ShouldLog(volatile LONG& counter) {
    LONG n = InterlockedIncrement(&counter);
    return n <= 25 || (n % 3000 == 0);
}

bool IniEnabled(LPCWSTR name, int defaultValue) {
    wchar_t path[MAX_PATH];
    HMODULE self;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)&IniEnabled, &self)) {
        return true;
    }
    GetModuleFileNameW(self, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"TownfallUltraWide.ini");
    return GetPrivateProfileIntW(L"Hooks", name, defaultValue, path) != 0;
}

double WindowAspect() {
    static HWND hwnd = nullptr;
    if (!hwnd) hwnd = FindWindowW(L"UnrealWindow", nullptr);
    if (!hwnd) return 0.0;
    RECT rc{};
    if (!GetClientRect(hwnd, &rc)) return 0.0;
    if (rc.bottom == 0) return 0.0;
    return static_cast<double>(rc.right) / rc.bottom;
}

struct IntRect {
    int left, top, right, bottom;
};

// ---- UCameraComponent::GetCameraView --------------------------------------

using GetCameraView_t = void (*)(void* self, float dt, void* view);
GetCameraView_t g_origGetCameraView = nullptr;
volatile LONG g_gcvCalls = 0;

void HookGetCameraView(void* self, float dt, void* view) {
    g_origGetCameraView(self, dt, view);
    if (g_origGetCameraView && ShouldLog(g_gcvCalls)) {
        float camFov = *reinterpret_cast<float*>(static_cast<char*>(self) + 0x240);
        float camAr = *reinterpret_cast<float*>(static_cast<char*>(self) + 0x264);
        float fmiFov = *reinterpret_cast<float*>(static_cast<char*>(view) + 0x30);
        float fmiAr = *reinterpret_cast<float*>(static_cast<char*>(view) + 0x5c);
        unsigned fmiFlags = *reinterpret_cast<unsigned*>(static_cast<char*>(view) + 0x68);
        LogLine("GetCameraView: camFov=%.2f camAR=%.4f | FMI fov=%.2f ar=%.4f constrain=%u | winA=%.4f",
                camFov, camAr, fmiFov, fmiAr, fmiFlags & 1, WindowAspect());
    }
}

// ---- FViewport::CalculateViewExtents (wrapper) ----------------------------

using CalcViewExtents_t = void (*)(void* outRect, void* viewport, void* x, void* inRect);
CalcViewExtents_t g_origCalcViewExtents = nullptr;
volatile LONG g_cveCalls = 0;

void HookCalcViewExtents(void* self, void* outRect, void* x, void* inRect) {
    IntRect before = *static_cast<IntRect*>(inRect);
    g_origCalcViewExtents(self, outRect, x, inRect);
    if (g_origCalcViewExtents && ShouldLog(g_cveCalls)) {
        IntRect after = *static_cast<IntRect*>(outRect);
        LogLine("CalcViewExtents: in=[%d %d %d %d] out=[%d %d %d %d] winA=%.4f",
                before.left, before.top, before.right, before.bottom, after.left, after.top,
                after.right, after.bottom, WindowAspect());
    }
}

// ---- FMinimalViewInfo::CalculateProjectionMatrixGivenView -----------------

using CalcProj_t = void (*)(void* fmi, unsigned char constraint, void* viewport, unsigned char* viewInit);
CalcProj_t g_origCalcProj = nullptr;
volatile LONG g_cpCalls = 0;

bool FixMatrixEnabled() {
    static int cached = -1;
    if (cached < 0) {
        wchar_t path[MAX_PATH];
        HMODULE self;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)&FixMatrixEnabled, &self)) {
            GetModuleFileNameW(self, path, MAX_PATH);
            wchar_t* slash = wcsrchr(path, L'\\');
            if (slash) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"TownfallUltraWide.ini");
        }
        cached = 1;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)&FixMatrixEnabled, &self)) {
            cached = GetPrivateProfileIntW(L"Patch", L"FixMatrix", 1, path);
        }
    }
    return cached != 0;
}

void HookCalcProj(void* fmi, unsigned char constraint, void* viewport, unsigned char* viewInit) {
    // Cameras with bConstrainAspectRatio (bit 0 of the FMinimalViewInfo flag
    // dword at +0x68) get a projection matrix built for the camera's authored
    // AspectRatio property (M00 = M11 / aspectProp) instead of the real view
    // rect aspect. The engine's matching pillarbox rect was already disabled
    // by the CalculateViewExtents patch; without clearing this flag the
    // authored-aspect image is stretched across the full 32:9 viewport.
    // Clearing the bit makes every camera project with M00 = M11 / rectAspect.
    if (FixMatrixEnabled()) {
        unsigned* flags = reinterpret_cast<unsigned*>(static_cast<char*>(fmi) + 0x68);
        *flags &= ~1u;
    }
    g_origCalcProj(fmi, constraint, viewport, viewInit);
    if (g_origCalcProj && ShouldLog(g_cpCalls)) {
        float fov = *reinterpret_cast<float*>(static_cast<char*>(fmi) + 0x30);
        float arProp = *reinterpret_cast<float*>(static_cast<char*>(fmi) + 0x5c);
        float f58 = *reinterpret_cast<float*>(static_cast<char*>(fmi) + 0x58);
        double m00 = *reinterpret_cast<double*>(viewInit + 0xA0);
        double m11 = *reinterpret_cast<double*>(viewInit + 0xC8);
        IntRect rect = *reinterpret_cast<IntRect*>(viewInit + 0x120);
        double ra = (rect.bottom != rect.top)
                        ? static_cast<double>(rect.right - rect.left) / (rect.bottom - rect.top)
                        : 0.0;
        LogLine("CalcProj: fov=%.2f arProp=%.4f f58=%.4f constraint=%u rectA=%.4f [%d %d %d %d] "
                "M00=%.6f M11=%.6f projA=%.4f winA=%.4f",
                fov, arProp, f58, constraint, ra, rect.left, rect.top, rect.right, rect.bottom,
                m00, m11, m11 != 0.0 ? m00 / m11 : 0.0, WindowAspect());
    }
}

}  // namespace

void InstallDiagnosticHooks(HMODULE game) {
    if (!IniEnabled(L"Enabled", 1)) {
        LogLine("hooks disabled via ini");
        return;
    }
    // UCameraComponent::GetCameraView exists as two byte-identical clones in
    // this binary; install on both (each InstallHook call patches the next
    // remaining match).
    static const unsigned char kSigGetCameraView[] = {
        0x48, 0x8B, 0xC4, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48, 0x8D, 0xA8, 0xF8, 0xFD, 0xFF,
        0xFF, 0x48, 0x81, 0xEC, 0xE0, 0x02, 0x00, 0x00, 0x0F, 0x29, 0x70, 0xC8, 0x0F, 0x29, 0x78,
        0xB8, 0x44, 0x0F, 0x29, 0x40, 0xA8, 0x44, 0x0F, 0x29, 0x48};
    if (IniEnabled(L"GetCameraView", 0))
        InstallHook(game, "GetCameraView", kSigGetCameraView, sizeof(kSigGetCameraView), 16,
                    reinterpret_cast<void*>(&HookGetCameraView),
                    reinterpret_cast<void**>(&g_origGetCameraView), /*allowMultiple=*/true);

    static const unsigned char kSigCalcViewExtents[] = {
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x68, 0x18, 0x48, 0x89, 0x70, 0x20,
        0x57, 0x48, 0x83, 0xEC, 0x40, 0x0F, 0x29, 0x70, 0xE8, 0x48, 0x8B, 0xEA, 0x48, 0x8B, 0x01,
        0x48, 0x8D, 0x54, 0x24, 0x50, 0x49, 0x8B, 0xF1, 0x0F, 0x28, 0xF2};
    if (IniEnabled(L"CalcViewExtents", 0))
        InstallHook(game, "CalcViewExtents", kSigCalcViewExtents, sizeof(kSigCalcViewExtents), 16,
                    reinterpret_cast<void*>(&HookCalcViewExtents),
                    reinterpret_cast<void**>(&g_origCalcViewExtents));

    static const unsigned char kSigCalcProj[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30,
        0xF3, 0x0F, 0x10, 0x91, 0x74, 0x08, 0x00, 0x00, 0x49, 0x8B, 0xF9, 0xF3, 0x0F, 0x58, 0x91,
        0x70, 0x08, 0x00, 0x00};
    if (IniEnabled(L"CalcProj", 1))  // carries the FixMatrix logic
        InstallHook(game, "CalcProj", kSigCalcProj, sizeof(kSigCalcProj), 15,
                    reinterpret_cast<void*>(&HookCalcProj), reinterpret_cast<void**>(&g_origCalcProj));
}
