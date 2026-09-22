// uiconstraint.cpp - Constrain the in-game HUD to a centered 16:9 / 21:9 box.
//
// Rendering order in UGameViewportClient::Draw (verified in-engine):
//   ApplySafeZone -> [3D scene via FCanvas] -> PopSafeZone -> SetCanvas ->
//   AHUD::DrawHUD -> ...
// so the safe-zone window wraps the SCENE, not the HUD. The HUD is drawn by
// AHUD::DrawHUD on the canvas passed to AHUD::SetCanvas. This module:
//
//   - hooks AHUD::SetCanvas to capture the HUD's UCanvas each frame
//   - hooks AHUD::DrawHUD and wraps the original call with an extra FCanvas
//     transform (scale into a centered box of the configured aspect) that is
//     pushed before and popped after the HUD draws
//
// DrawHUD only runs while a HUD exists, so menus / inventory / pause screens
// (pure Slate/UMG) are untouched by construction. UCanvas+0x2e0 holds the
// FCanvas transform-stack TArray (FCanvas+0x28); PushAbsoluteTransform takes
// the FCanvas base, the TArray::Pop takes the stack.

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

using SetCanvas_t = void (*)(void* hud, void* canvas);
using DrawHUD_t = void (*)(void* hud);
using PushAbsoluteTransform_t = void (*)(void* fcanvas, const float* matrix);
using TransformStackPop_t = void (*)(void* stack);

SetCanvas_t g_origSetCanvas = nullptr;
DrawHUD_t g_origDrawHUD = nullptr;
PushAbsoluteTransform_t gPush = nullptr;
TransformStackPop_t gStackPop = nullptr;

void* gHudCanvas = nullptr;  // UCanvas of the current HUD frame

void PushBoxTransform() {
    if (!gHudCanvas) return;
    char* canvas = static_cast<char*>(gHudCanvas);
    const float W = static_cast<float>(*reinterpret_cast<int*>(canvas + 0x40));
    const float H = static_cast<float>(*reinterpret_cast<int*>(canvas + 0x44));
    if (W <= 0.f || H <= 0.f) return;

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
    void* stack = *reinterpret_cast<void**>(canvas + 0x2e0);
    if (!stack) return;
    void* fcanvas = static_cast<char*>(stack) - 0x28;
    gPush(fcanvas, m);
}

void PopBoxTransform() {
    if (!gHudCanvas) return;
    void* stack = *reinterpret_cast<void**>(static_cast<char*>(gHudCanvas) + 0x2e0);
    if (stack) gStackPop(stack);
}

void HookSetCanvas(void* hud, void* canvas) {
    while (!g_origSetCanvas) Sleep(0);
    gHudCanvas = canvas;
    g_origSetCanvas(hud, canvas);
}

void HookDrawHUD(void* hud) {
    while (!g_origDrawHUD) Sleep(0);
    if (gConstrain && gPush && gStackPop) {
        PushBoxTransform();
        g_origDrawHUD(hud);
        PopBoxTransform();
        return;
    }
    g_origDrawHUD(hud);
}

}  // namespace

void InstallUIConstraint(HMODULE game) {
    LoadConfig();
    if (!ConfigInt(L"Enabled", 1)) {
        LogLine("UI: module disabled via ini");
        return;
    }

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

    static const unsigned char kSigSetCanvas[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20,
        0x80, 0x3D, 0x87, 0x24};
    InstallHook(game, "HUDSetCanvas", kSigSetCanvas, sizeof(kSigSetCanvas), 15,
                reinterpret_cast<void*>(&HookSetCanvas),
                reinterpret_cast<void**>(&g_origSetCanvas));

    static const unsigned char kSigDrawHUD[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x48, 0x8D, 0x6C, 0x24, 0xA9, 0x48, 0x81, 0xEC, 0xA0,
        0x00, 0x00, 0x00, 0x4C, 0x8D, 0x89, 0x28, 0x03, 0x00, 0x00, 0x48, 0x8B, 0xD9, 0x49, 0x8D,
        0x49, 0x0C, 0x8B, 0x01, 0x41, 0xC7, 0x41, 0x08, 0x00, 0x00};
    InstallHook(game, "HUDDrawHUD", kSigDrawHUD, sizeof(kSigDrawHUD), 18,
                reinterpret_cast<void*>(&HookDrawHUD), reinterpret_cast<void**>(&g_origDrawHUD));
}
