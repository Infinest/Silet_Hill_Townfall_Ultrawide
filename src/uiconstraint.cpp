// uiconstraint.cpp - Constrain the in-game HUD to a centered 16:9 / 21:9 box.
//
// The Townfall HUD (health/stamina bars, item slots, prompts) is drawn with
// the classic FCanvas immediate path inside UGameViewportClient::Draw - not
// through SCanvas/UMG slots (verified empirically). Draw wraps HUD rendering
// in UCanvas::ApplySafeZoneTransform / PopSafeZoneTransform every frame; with
// zero safe-zone margins both are no-ops.
//
// With [UI] Constrain=1 the Apply hook pushes an extra FCanvas transform
// (scale into a centered box of the configured aspect) and the Pop hook pops
// it again right after the HUD section. Menus, inventory and pause screens
// render via Slate/UMG and are untouched; no game-state detection is needed
// because the safe-zone pair only wraps the HUD draw.

#include <windows.h>

#include <cstdint>
#include <cwchar>

#include "detour.h"
#include "log.h"

namespace {

bool ConfigInt(LPCWSTR key, int def) {
    wchar_t path[MAX_PATH];
    HMODULE self;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)&ConfigInt, &self)) {
        return def;
    }
    GetModuleFileNameW(self, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"TownfallUltraWide.ini");
    return GetPrivateProfileIntW(L"UI", key, def, path);
}

bool ConfigAspect(double* out) {
    wchar_t path[MAX_PATH], buf[32] = {};
    HMODULE self;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)&ConfigAspect, &self)) {
        return false;
    }
    GetModuleFileNameW(self, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"TownfallUltraWide.ini");
    GetPrivateProfileStringW(L"UI", L"Aspect", L"16:9", buf, 32, path);
    int w = 0, h = 0;
    if (swscanf_s(buf, L"%d:%d", &w, &h) != 2 || w <= 0 || h <= 0) return false;
    *out = static_cast<double>(w) / h;
    return true;
}

bool gConstrain = false;
double gAspect = 16.0 / 9.0;
bool gCfgLoaded = false;

void LoadConfig() {
    if (gCfgLoaded) return;
    gCfgLoaded = true;
    gConstrain = ConfigInt(L"Constrain", 0) != 0;
    if (!ConfigAspect(&gAspect)) gAspect = 16.0 / 9.0;
    LogLine("UI: Constrain=%d Aspect=%.4f", gConstrain ? 1 : 0, gAspect);
}

using ApplySafeZone_t = void (*)(void* canvas);
using PopSafeZone_t = void (*)(void* canvas);
using PushAbsoluteTransform_t = void (*)(void* fcanvas, const float* matrix);
using TransformStackPop_t = void (*)(void* stack);

ApplySafeZone_t g_origApply = nullptr;
PopSafeZone_t g_origPop = nullptr;
PushAbsoluteTransform_t gPush = nullptr;
TransformStackPop_t gStackPop = nullptr;

bool gTransformPushed = false;  // HUD draw is single-threaded; plain bool is fine

void HookApplySafeZone(void* canvas) {
    while (!g_origApply) Sleep(0);
    if (gConstrain && gPush && gStackPop) {
        const float W = static_cast<float>(*reinterpret_cast<int*>(static_cast<char*>(canvas) + 0x40));
        const float H = static_cast<float>(*reinterpret_cast<int*>(static_cast<char*>(canvas) + 0x44));
        if (W > 0.f && H > 0.f) {
            float boxW, boxH;
            if (static_cast<double>(W) / H > gAspect) {
                boxH = H;
                boxW = static_cast<float>(H * gAspect);
            } else {
                boxW = W;
                boxH = static_cast<float>(W / gAspect);
            }
            const float m[16] = {boxW / W, 0.f,      0.f, 0.f,  //
                                 0.f,      boxH / H, 0.f, 0.f,  //
                                 0.f,      0.f,      1.f, 0.f,  //
                                 (W - boxW) / 2.f, (H - boxH) / 2.f, 0.f, 1.f};
            void* fcanvas = *reinterpret_cast<void**>(static_cast<char*>(canvas) + 0x2e0);
            if (fcanvas) {
                gPush(fcanvas, m);
                gTransformPushed = true;
            }
        }
    }
    g_origApply(canvas);
}

void HookPopSafeZone(void* canvas) {
    while (!g_origPop) Sleep(0);
    if (gTransformPushed && gStackPop) {
        gTransformPushed = false;
        void* fcanvas = *reinterpret_cast<void**>(static_cast<char*>(canvas) + 0x2e0);
        if (fcanvas) gStackPop(fcanvas);  // TArray<FTransformEntry> at FCanvas+0
    }
    g_origPop(canvas);
}

}  // namespace

void InstallUIConstraint(HMODULE game) {
    LoadConfig();
    if (!ConfigInt(L"Enabled", 1)) {
        LogLine("UI: module disabled via ini");
        return;
    }

    // Functions called (not hooked): FCanvas::PushAbsoluteTransform and the
    // transform-stack TArray::Pop used by the engine's own safe-zone pop.
    static const unsigned char kSigPush[] = {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C,
        0x24, 0xC0, 0x48, 0x81, 0xEC, 0x40, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x11};
    gPush = reinterpret_cast<PushAbsoluteTransform_t>(
        FindUniquePattern(game, kSigPush, sizeof(kSigPush)));
    static const unsigned char kSigStackPop[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x4C, 0x63, 0x59, 0x08, 0xB8, 0x80, 0x00, 0x00, 0x00,
        0x4C};
    gStackPop = reinterpret_cast<TransformStackPop_t>(
        FindUniquePattern(game, kSigStackPop, sizeof(kSigStackPop)));
    if (!gPush || !gStackPop) {
        LogLine("UI: transform resolve failed gPush=%llX gStackPop=%llX - disabled",
                (unsigned long long)(uintptr_t)gPush, (unsigned long long)(uintptr_t)gStackPop);
        return;
    }

    static const unsigned char kSigApply[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20,
        0x41, 0x56, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00};
    InstallHook(game, "ApplySafeZone", kSigApply, sizeof(kSigApply), 17,
                reinterpret_cast<void*>(&HookApplySafeZone), reinterpret_cast<void**>(&g_origApply));

    static const unsigned char kSigPop[] = {
        0x40, 0x53, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00, 0x83, 0xB9, 0x90, 0x00, 0x00, 0x00};
    InstallHook(game, "PopSafeZone", kSigPop, sizeof(kSigPop), 15,
                reinterpret_cast<void*>(&HookPopSafeZone), reinterpret_cast<void**>(&g_origPop));
}
