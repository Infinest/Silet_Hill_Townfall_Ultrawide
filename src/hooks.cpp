// hooks.cpp - camera hooks that replace the former 32:9 byte patch.
//
// FMinimalViewInfo::CalculateProjectionMatrixGivenView @ 0x1798088
//     rcx = FMinimalViewInfo, dl = EAspectRatioAxisConstraint,
//     r8 = FViewport, r9 = FSceneViewInitOptions& (ProjectionMatrix @ +0xA0,
//     ViewRectangle @ +0x120)
//
// FViewport::CalculateViewExtents (inner rect adjustment) @ 0x1798174
//     rcx = FIntRect& out, r9 = FIntRect& in; returns the out rect. Vanilla
//     behavior: pillarbox/letterbox the rect to the camera's authored aspect.
//
// Two coordinated decisions are made per view:
//
//  - CalculateViewExtents: for normal cameras the view rect is handed through
//    unchanged (the former jbe->jmp byte patch, now conditional). For
//    cutscene cameras with [Camera] DisableInCutscenes=1 the original engine
//    function runs, reproducing the vanilla pillarbox exactly.
//
//  - CalculateProjectionMatrixGivenView: for normal cameras the
//    bConstrainAspectRatio flag (bit 0 of the FMinimalViewInfo flag dword at
//    +0x68) is cleared so the matrix is built from the real view rect aspect
//    (M00 = M11 / rectAspect) instead of the authored AspectRatio property
//    (which would stretch the authored-aspect image across the full 32:9
//    viewport). Cutscene cameras use constraint 1 (MaintainXFOV): the FOV
//    property is the horizontal FOV and the vertical FOV is derived from the
//    real view rect, vHalf = atan(tan(FOV/2) / rectAspect) - at the full 32:9
//    rect that crops the vertical FOV (zoomed-in image). They are switched to
//    constraint 0 (only when an authored AspectRatio property at +0x5c
//    exists), which yields the rect-independent authored vertical FOV
//    2*atan(tan(FOV/2) / aspectProp) so the picture only widens horizontally.
//    With DisableInCutscenes=1 cutscene cameras are left 100% vanilla instead.

#include <windows.h>

#include <cstdint>

#include "detour.h"
#include "log.h"

namespace {

using CalcProj_t = void (*)(void* fmi, unsigned char constraint, void* viewport,
                            unsigned char* viewInit);
CalcProj_t g_origCalcProj = nullptr;

using CalcViewExtents_t = void* (*)(void* outRect, void* a, void* b, void* inRect);
CalcViewExtents_t g_origCalcViewExtents = nullptr;

// Set at CalculateProjectionMatrixGivenView entry, consumed by the
// CalculateViewExtents hook later in the same view setup (same thread).
bool gKeepViewRect = false;

// [Camera] DisableInCutscenes (0/1, default 0): leave cutscenes 100% vanilla
// (pillarboxed to the camera's authored aspect) instead of super ultrawide.
bool CutscenePillarboxEnabled() {
    static int cached = -1;
    if (cached < 0) {
        cached = 0;
        wchar_t path[MAX_PATH];
        HMODULE self;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)&CutscenePillarboxEnabled, &self)) {
            GetModuleFileNameW(self, path, MAX_PATH);
            wchar_t* slash = wcsrchr(path, L'\\');
            if (slash) {
                wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"TownfallUltraWide.ini");
                cached = GetPrivateProfileIntW(L"Camera", L"DisableInCutscenes", 0, path) != 0;
            }
        }
    }
    return cached != 0;
}

void HookCalcProj(void* fmi, unsigned char constraint, void* viewport, unsigned char* viewInit) {
    gKeepViewRect = false;

    if (constraint == 1) {
        // Cutscene camera (MaintainXFOV).
        const float aspect = *reinterpret_cast<float*>(static_cast<char*>(fmi) + 0x5c);
        if (aspect > 0.f) {
            if (CutscenePillarboxEnabled()) {
                // Vanilla cutscene: keep bConstrainAspectRatio, keep the
                // constraint, and let the engine adjust the view rect.
                gKeepViewRect = true;
                g_origCalcProj(fmi, constraint, viewport, viewInit);
                return;
            }
            // Ultrawide cutscene: switch to constraint 0 so the authored
            // vertical FOV is preserved and the picture only widens
            // horizontally (same framing behavior as gameplay).
            constraint = 0;
        }
    }

    // Normal camera: build the projection from the real view rect aspect.
    unsigned* flags = reinterpret_cast<unsigned*>(static_cast<char*>(fmi) + 0x68);
    *flags &= ~1u;
    g_origCalcProj(fmi, constraint, viewport, viewInit);
}

void* HookCalcViewExtents(void* outRect, void* a, void* b, void* inRect) {
    if (!gKeepViewRect) {
        // Patch active: present the incoming rect unchanged (the former
        // jbe->jmp byte patch), skipping the engine's pillarbox adjustment.
        reinterpret_cast<long long*>(outRect)[0] = reinterpret_cast<long long*>(inRect)[0];
        reinterpret_cast<long long*>(outRect)[1] = reinterpret_cast<long long*>(inRect)[1];
        return outRect;
    }
    // Cutscene with DisableInCutscenes=1: vanilla pillarbox adjustment.
    return g_origCalcViewExtents(outRect, a, b, inRect);
}

}  // namespace

void InstallCameraHooks(HMODULE game) {
    static const unsigned char kSigCalcProj[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30,
        0xF3, 0x0F, 0x10, 0x91, 0x74, 0x08, 0x00, 0x00, 0x49, 0x8B, 0xF9, 0xF3, 0x0F, 0x58, 0x91,
        0x70, 0x08, 0x00, 0x00};
    InstallHook(game, "CalcProj", kSigCalcProj, sizeof(kSigCalcProj), 15,
                reinterpret_cast<void*>(&HookCalcProj),
                reinterpret_cast<void**>(&g_origCalcProj));

    // CalculateViewExtents inner rect adjustment. 21-byte branch-free,
    // rsp-safe prologue (copied verbatim into the trampoline).
    static const unsigned char kSigCalcViewExtents[] = {
        0x49, 0x8B, 0x01, 0x4C, 0x8B, 0xC1, 0x45, 0x8B, 0x51, 0x04, 0x48, 0x89, 0x01,
        0x49, 0x8B, 0x41, 0x08, 0x48, 0x89, 0x41, 0x08};
    InstallHook(game, "CalcViewExtents", kSigCalcViewExtents, sizeof(kSigCalcViewExtents), 21,
                reinterpret_cast<void*>(&HookCalcViewExtents),
                reinterpret_cast<void**>(&g_origCalcViewExtents));
}
