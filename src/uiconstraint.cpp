// uiconstraint.cpp - Constrain the in-game HUD to a centered 16:9 / 21:9 box.
//
// The visible HUD is Slate/UMG: UUserWidget::AddToViewport ->
// UGameViewportClient::AddViewportWidgetContent -> SOverlay::AddSlot on the
// viewport overlay (UGameViewportClient+0x120). All HUD/menu widgets are
// children of that SOverlay; SOverlay::OnArrangeChildren (vtable slot 71)
// hands each child its FGeometry. Hooking that virtual and presenting the
// children a modified (centered box) allotted geometry constrains the UI
// without touching a single draw call.
//
// UGameViewportClient is reached via SGameLayerManager (captured from a hook
// on GetGameViewportDPIScale, rcx = manager):
//   manager+0x2f8 TAttribute<FSceneViewport*> -> Get() -> FSceneViewport
//   [FSV+0x38] = UGameViewportClient; [GVC+0x120] = ViewportOverlay

#include <windows.h>

#include <cstdint>
#include <cwchar>

#include "detour.h"
#include "log.h"

namespace {

int ConfigInt(LPCWSTR key, int def) {
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

// Constrain: 0 = off, 1 = centered 16:9 box, 2 = centered 21:9 box.
int gConstrain = 0;
double gAspect = 16.0 / 9.0;
bool gCfgLoaded = false;

void LoadConfig() {
    if (gCfgLoaded) return;
    gCfgLoaded = true;
    gConstrain = ConfigInt(L"Constrain", 1);
    if (gConstrain < 0 || gConstrain > 2) gConstrain = 1;
    gAspect = (gConstrain == 2) ? 21.0 / 9.0 : 16.0 / 9.0;
    LogLine("UI: Constrain=%d", gConstrain);
}


// ---- gameplay state gate (UTownfallGameInstance EGameState stack) ---------

bool IsGamePtr(void* p);  // defined below

void* gGameInstance = nullptr;
int g_lastState = -2;

int CurrentState() {
    if (!gGameInstance) return -1;
    if (!IsGamePtr(gGameInstance)) {
        gGameInstance = nullptr;
        return -1;
    }
    const char* data = *reinterpret_cast<const char**>(static_cast<char*>(gGameInstance) + 0x298);
    const int count = *reinterpret_cast<const int*>(static_cast<char*>(gGameInstance) + 0x2a0);
    if (!data || count <= 0 || count > 64) return -1;
    return static_cast<unsigned char>(data[count - 1]);
}

bool GameplayGate() {
    // The HUD is constrained only while the game-state stack top is 4
    // (gameplay). Menus, loading, inventory and pause push other states.
    const int state = CurrentState();
    if (state != g_lastState) {
        LogLine("UI: game state -> %d", state);
        g_lastState = state;
    }
    return state == 4;
}

// ---- context capture (SGameLayerManager -> UGameViewportClient) -----------

using AttrGet_t = void* (*)(void* attr);
using DpiScale_t = float (*)(void* manager);
using Arrange_t = void (*)(void* this_, void* allottedGeometry, void* arrangedChildren);
using AddSlot_t = void* (*)(void* this_, void* slotArgs);

AttrGet_t gAttrGet = nullptr;
DpiScale_t g_origDpi = nullptr;
Arrange_t g_origArrange = nullptr;
AddSlot_t g_origAddSlot = nullptr;

void* gViewportOverlay = nullptr;
uintptr_t gGameBase = 0;
uintptr_t gGameEnd = 0;

bool IsGamePtr(void* p) {
    if (!gGameBase) {
        HMODULE game = GetModuleHandleW(nullptr);
        gGameBase = reinterpret_cast<uintptr_t>(game);
        gGameEnd = gGameBase + 0xA978000;
    }
    if (reinterpret_cast<uintptr_t>(p) <= 0x10000) return false;
    void* vt = *reinterpret_cast<void**>(p);
    return reinterpret_cast<uintptr_t>(vt) >= gGameBase &&
           reinterpret_cast<uintptr_t>(vt) < gGameEnd;
}

int gCaptureAttempts = 0;

void CaptureContext(void* manager) {
    if (!gAttrGet || gCaptureAttempts >= 400) return;
    ++gCaptureAttempts;
    if (!IsGamePtr(manager)) return;

    void* fsv = nullptr;
    void* attrSlot = nullptr;
    __try {
        attrSlot = gAttrGet(static_cast<char*>(manager) + 0x2f8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;  // manager still under construction - retry later
    }
    if (attrSlot && reinterpret_cast<uintptr_t>(attrSlot) > 0x10000)
        fsv = *reinterpret_cast<void**>(attrSlot);
    if (!IsGamePtr(fsv)) return;
    void* gvc = *reinterpret_cast<void**>(static_cast<char*>(fsv) + 0x38);
    if (!IsGamePtr(gvc)) return;
    if (gViewportOverlay == nullptr) {
        void* overlay = *reinterpret_cast<void**>(static_cast<char*>(gvc) + 0x120);
        if (IsGamePtr(overlay)) {
            gViewportOverlay = overlay;
            LogLine("UI: viewport overlay captured %llX", (unsigned long long)(uintptr_t)overlay);
        }
    }
}

// AddSlot instance counting: the ViewportOverlay is the SOverlay that
// receives the AddToViewport widget bursts.
struct SlotCount {
    void* instance;
    int count;
};
SlotCount g_slotCounts[32] = {};

void* HookAddSlot(void* this_, void* slotArgs) {
    while (!g_origAddSlot) Sleep(0);
    if (gViewportOverlay == nullptr && IsGamePtr(this_)) {
        for (auto& sc : g_slotCounts) {
            if (sc.instance == this_) {
                ++sc.count;
                break;
            }
            if (sc.instance == nullptr) {
                sc.instance = this_;
                sc.count = 1;
                break;
            }
        }
        // the first instance to reach 4 add-slot calls is almost certainly the
        // viewport overlay (game widgets arrive in bursts)
        for (auto& sc : g_slotCounts) {
            if (sc.instance == this_ && sc.count >= 4 && gViewportOverlay == nullptr) {
                gViewportOverlay = this_;
                LogLine("UI: overlay via addslot %llX", (unsigned long long)(uintptr_t)this_);
            }
        }
    }
    return g_origAddSlot(this_, slotArgs);
}

float HookDpi(void* manager) {
    while (!g_origDpi) Sleep(0);
    LoadConfig();
    CaptureContext(manager);
    return g_origDpi(manager);
}

// ---- GameInstance capture (UTownfallGameInstance::PopGameState) -----------

using PopState_t = void (*)(void* this_, unsigned char state);

PopState_t g_origPop = nullptr;

void HookPop(void* this_, unsigned char state) {
    while (!g_origPop) Sleep(0);
    if (gGameInstance == nullptr && IsGamePtr(this_)) {
        gGameInstance = this_;
        LogLine("UI: game instance captured %llX", (unsigned long long)(uintptr_t)this_);
    }
    g_origPop(this_, state);
}

// ---- SOverlay::OnArrangeChildren (vtable slot 71) -------------------------

void HookArrange(void* this_, void* geo, void* arranged) {
    // Fallback overlay identification if the AddSlot path has not found it
    // yet: the viewport overlay is the SOverlay arranged with a
    // full-display-width geometry.
    if (gViewportOverlay == nullptr) {
        float w = *reinterpret_cast<float*>(geo);
        if (w > 3000.f && w < 20000.f) {
            int slots = *reinterpret_cast<int*>(static_cast<char*>(this_) + 0x200);
            if (slots >= 2) {
                static void* lastCandidate = nullptr;
                static int lastCandidateHits = 0;
                if (this_ == lastCandidate) {
                    if (++lastCandidateHits >= 5) gViewportOverlay = this_;
                } else {
                    lastCandidate = this_;
                    lastCandidateHits = 1;
                }
            }
        }
    }
    if (this_ == gViewportOverlay && GameplayGate()) {
        // Present the children a centered box. FGeometry (double precision):
        //   +0x00 Size, +0x10 Position, +0x20 AbsolutePosition (FVector2D
        //   doubles each), +0x30 AbsoluteScale.
        // FGeometry layout (from the PDB via DIA, len 0x38, float-based):
        //   +0x00 Size (FVector2f), +0x08 Scale, +0x0C AbsolutePosition,
        //   +0x14 Position (local), +0x1C AccumulatedRenderTransform,
        //   +0x34 bHasRenderTransform.
        char* g = static_cast<char*>(geo);
        const float W = *reinterpret_cast<float*>(g + 0x00);
        const float H = *reinterpret_cast<float*>(g + 0x04);
        const float scale = *reinterpret_cast<float*>(g + 0x08);
        if (W > 0.f && H > 0.f && scale > 0.f) {
            float boxW, boxH;
            if (static_cast<double>(W) / H > gAspect) {
                boxH = H;
                boxW = static_cast<float>(H * gAspect);
            } else {
                boxW = W;
                boxH = static_cast<float>(W / gAspect);
            }
            const float ox = (W - boxW) / 2.f;   // box origin, local units
            const float oy = (H - boxH) / 2.f;
            const float oxAbs = ox * scale;      // AbsolutePosition/render
            const float oyAbs = oy * scale;      //   transform are in px
            char copy[0x60];
            memcpy(copy, geo, sizeof(copy));
            *reinterpret_cast<float*>(copy + 0x00) = boxW;
            *reinterpret_cast<float*>(copy + 0x04) = boxH;
            *reinterpret_cast<float*>(copy + 0x0C) += oxAbs;  // AbsolutePosition
            *reinterpret_cast<float*>(copy + 0x10) += oyAbs;
            *reinterpret_cast<float*>(copy + 0x14) += ox;  // Position (local)
            *reinterpret_cast<float*>(copy + 0x18) += oy;
            *reinterpret_cast<float*>(copy + 0x2C) += oxAbs;  // AccumulatedRender
            *reinterpret_cast<float*>(copy + 0x30) += oyAbs;  //   Transform translation
            g_origArrange(this_, copy, arranged);
            return;
        }
    }
    g_origArrange(this_, geo, arranged);
}

bool HookVtableSlot(uintptr_t vtableRva, int slot, uintptr_t expectedTarget, void* wrapper,
                    const char* name) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    void** entry = reinterpret_cast<void**>(base + vtableRva);
    void** slotPtr = entry + slot;
    if (*slotPtr != reinterpret_cast<void*>(base + expectedTarget)) {
        LogLine("UI: %s slot mismatch (got %llX) - not hooked", name,
                (unsigned long long)(uintptr_t)*slotPtr);
        return false;
    }
    DWORD oldProtect = 0;
    if (!VirtualProtect(slotPtr, 8, PAGE_READWRITE, &oldProtect)) return false;
    *slotPtr = wrapper;
    VirtualProtect(slotPtr, 8, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), slotPtr, 8);
    return true;
}

}  // namespace

void InstallUIConstraint(HMODULE game) {
    LoadConfig();
    if (gConstrain == 0) {
        LogLine("UI: constraint off (Constrain=0)");
        return;
    }

    static const unsigned char kSigAttrGet[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xFF, 0x48, 0x8B, 0xD9,
        0x39, 0x79, 0x18, 0x74, 0x09, 0x48, 0x8B, 0x49};
    gAttrGet = reinterpret_cast<AttrGet_t>(FindUniquePattern(game, kSigAttrGet, sizeof(kSigAttrGet)));
    if (!gAttrGet) {
        LogLine("UI: attribute getter not found - disabled");
        return;
    }

    static const unsigned char kSigAddSlot[] = {
        0x48, 0x89, 0x6C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20,
        0x41, 0x56, 0x48, 0x81, 0xEC, 0xE0, 0x00};
    InstallHook(game, "SOverlayAddSlot", kSigAddSlot, sizeof(kSigAddSlot), 17,
                reinterpret_cast<void*>(&HookAddSlot), reinterpret_cast<void**>(&g_origAddSlot));

    static const unsigned char kSigDpi[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20,
        0x48, 0x8B, 0xF9, 0x48, 0x81};
    InstallHook(game, "GameViewportDPIScale", kSigDpi, sizeof(kSigDpi), 15,
                reinterpret_cast<void*>(&HookDpi), reinterpret_cast<void**>(&g_origDpi));

    // PopGameState: rcx = UTownfallGameInstance on every state transition.
    // 15-byte rsp-relative prologue, no branches (trampoline-safe).
    static const unsigned char kSigPop[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57,
        0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB6, 0xDA, 0x48, 0x8B, 0xF9};
    InstallHook(game, "PopGameState", kSigPop, sizeof(kSigPop), 15,
                reinterpret_cast<void*>(&HookPop), reinterpret_cast<void**>(&g_origPop));

    g_origArrange = reinterpret_cast<Arrange_t>(
        reinterpret_cast<uintptr_t>(game) + 0x12D9EF4);
    if (!HookVtableSlot(0x79ECA08, 71, 0x12D9EF4, reinterpret_cast<void*>(&HookArrange),
                        "SOverlay::OnArrangeChildren")) {
        LogLine("UI: arrange hook failed - disabled");
        return;
    }
    LogLine("UI: SOverlay arrange hooked");
}
