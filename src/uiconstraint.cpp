// uiconstraint.cpp - Constrain the in-game UI to a centered 16:9 / 21:9 box.
//
// All fullscreen UMG UI (HUD, menus, inventory) is hosted as slots in a single
// SCanvas owned by SGameLayerManager (canvas pointer at manager+0x388; each
// FSlot stores its owner canvas at slot+0x18, its Position attribute at
// slot+0x30 and its Size attribute at slot+0x40; SCanvas::FSlot::SetPosition /
// SetSize are the writers). The hooks below rewrite Position/Size to a
// centered box of the configured aspect ratio - but ONLY while the game's
// state stack top (UTownfallGameInstance, TArray<EGameState> at +0x298,
// count at +0x2a0) is one of the allowed gameplay states, so menus, inventory
// and pause screens keep full-width UI.
//
// Offsets (UE 5.6.1, Townfall 1.00):
//   SGameLayerManager +0x2f8  TAttribute<FSceneViewport*>
//   FSceneViewport    +0x38   UGameViewportClient
//   UGameViewportClient+0x78  UWorld
//   UWorld            +0x228  UGameInstance
//   UTownfallGameInstance+0x298/+0x2a0  EGameState stack data/count

#include <windows.h>

#include <cstdint>
#include <cwchar>

#include "detour.h"
#include "log.h"

namespace {

// ---- runtime state ---------------------------------------------------------

void* gManager = nullptr;
void* gCanvas = nullptr;      // SCanvas of the game layer manager
void* gGameInstance = nullptr;

using AttrGet_t = void* (*)(void* attr);
AttrGet_t gAttrGet = nullptr;

// Box in canvas-local pixels, derived from the last SetSize call.
double gBoxX = 0.0, gBoxY = 0.0, gBoxW = 0.0, gBoxH = 0.0;

// ---- config (read once) ----------------------------------------------------

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

bool ConfigAspect(double* out) {  // returns false if not "W:H"
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

struct Config {
    bool constrain = false;
    bool logStates = true;
    double aspect = 16.0 / 9.0;
    int allowed[16] = {};
    int allowedCount = 0;
};

Config gCfg;
bool gCfgLoaded = false;

void LoadConfig() {
    if (gCfgLoaded) return;
    gCfgLoaded = true;
    gCfg.constrain = ConfigInt(L"Constrain", 0) != 0;
    gCfg.logStates = ConfigInt(L"LogStates", 1) != 0;
    if (!ConfigAspect(&gCfg.aspect)) gCfg.aspect = 16.0 / 9.0;

    wchar_t path[MAX_PATH], buf[128] = {};
    HMODULE self;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)&LoadConfig, &self)) {
        GetModuleFileNameW(self, path, MAX_PATH);
        wchar_t* slash = wcsrchr(path, L'\\');
        if (slash) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"TownfallUltraWide.ini");
        GetPrivateProfileStringW(L"UI", L"AllowedStates", L"", buf, 128, path);
        wchar_t* ctx = nullptr;
        for (wchar_t* tok = wcstok_s(buf, L", ;\t", &ctx);
             tok && gCfg.allowedCount < 16; tok = wcstok_s(nullptr, L", ;\t", &ctx)) {
            gCfg.allowed[gCfg.allowedCount++] = _wtoi(tok);
        }
    }
    LogLine("UI: Constrain=%d LogStates=%d Aspect=%.4f AllowedStates=%d entries",
            gCfg.constrain ? 1 : 0, gCfg.logStates ? 1 : 0, gCfg.aspect, gCfg.allowedCount);
}

// ---- game state ------------------------------------------------------------

int CurrentState() {
    if (!gGameInstance) return -1;
    const char* data = *reinterpret_cast<const char**>(static_cast<char*>(gGameInstance) + 0x298);
    const int count = *reinterpret_cast<const int*>(static_cast<char*>(gGameInstance) + 0x2a0);
    if (!data || count <= 0) return -1;
    return static_cast<unsigned char>(data[count - 1]);
}

bool GameplayGate() {
    if (!gCfg.constrain || gCfg.allowedCount == 0) return false;
    const int state = CurrentState();
    for (int i = 0; i < gCfg.allowedCount; ++i)
        if (gCfg.allowed[i] == state) return true;
    return false;
}

int gCaptureAttempts = 0;
uintptr_t gGameBase = 0;
uintptr_t gGameEnd = 0;

// Every engine object we chase has a vtable inside the game module; validate
// that before each dereference so a bad hop fails cleanly instead of AVing.
bool IsGameObject(void* p) {
    if (!gGameBase) {
        HMODULE game = GetModuleHandleW(nullptr);
        gGameBase = reinterpret_cast<uintptr_t>(game);
        gGameEnd = gGameBase + 0xA978000;  // image size of Townfall 1.00
    }
    if (reinterpret_cast<uintptr_t>(p) <= 0x10000) return false;
    void* vt = *reinterpret_cast<void**>(p);  // caller ensures p is readable-ish;
    // vtables of engine objects live in the game exe
    return reinterpret_cast<uintptr_t>(vt) >= gGameBase &&
           reinterpret_cast<uintptr_t>(vt) < gGameEnd;
}

void* ReadObj(void* base, uintptr_t offset) {
    if (!IsGameObject(base)) return nullptr;
    return *reinterpret_cast<void**>(static_cast<char*>(base) + offset);
}

void CaptureContext(void* manager) {
    if (gGameInstance || !gAttrGet || gCaptureAttempts >= 40) return;
    ++gCaptureAttempts;
    if (!IsGameObject(manager)) return;
    gManager = manager;
    gCanvas = ReadObj(manager, 0x388);

    // TAttribute<FSceneViewport*>::Get returns &storedValue; the value itself
    // may still be null during early boot - retry on later calls.
    void* attrSlot = gAttrGet(static_cast<char*>(manager) + 0x2f8);
    if (!attrSlot || reinterpret_cast<uintptr_t>(attrSlot) <= 0x10000) return;
    void* fsv = *reinterpret_cast<void**>(attrSlot);
    void* world = nullptr;
    void* gvc = ReadObj(fsv, 0x38);
    if (gvc) {
        // Replicate the engine chain exactly: [[fsv+0x38]]->vtable[+0x30]() is
        // the GetWorld virtual (see SGameLayerManager::UpdateLayout).
        void** vt = *reinterpret_cast<void***>(gvc);
        uintptr_t vta = reinterpret_cast<uintptr_t>(vt);
        if (vta >= gGameBase && vta < gGameEnd) {
            using GetWorldFn = void* (*)(void*);
            world = reinterpret_cast<GetWorldFn>(vt[0x30 / 8])(gvc);
        }
    }
    void* gi = ReadObj(world, 0x228);
    LogLine("UI: hops attempt=%d manager=%llX attr=%llX fsv=%llX gvc=%llX world=%llX gi=%llX",
            gCaptureAttempts, (unsigned long long)(uintptr_t)manager,
            (unsigned long long)(uintptr_t)attrSlot, (unsigned long long)(uintptr_t)fsv,
            (unsigned long long)(uintptr_t)gvc, (unsigned long long)(uintptr_t)world,
            (unsigned long long)(uintptr_t)gi);
    if (!gi) return;
    gGameInstance = gi;
    LogLine("UI: context captured");
}

// ---- hooks -----------------------------------------------------------------

using DpiScale_t = float (*)(void* manager);
DpiScale_t g_origDpiScale = nullptr;
int g_lastState = -2;

float HookDpiScale(void* manager) {
    while (!g_origDpiScale) Sleep(0);
    LoadConfig();
    CaptureContext(manager);
    if (gCfg.logStates) {
        const int state = CurrentState();
        if (state != g_lastState) {
            LogLine("UI: game state -> %d", state);
            g_lastState = state;
        }
    }
    return g_origDpiScale(manager);
}

bool AppliesToSlot(void* slot) {
    return gCfg.constrain && gCanvas && *reinterpret_cast<void**>(static_cast<char*>(slot) + 0x18) == gCanvas;
}

using SetVec_t = void (*)(void* slot, double* vec);
SetVec_t g_origSetSize = nullptr;
SetVec_t g_origSetPosition = nullptr;

void HookSetSize(void* slot, double* vec) {
    while (!g_origSetSize) Sleep(0);
    if (AppliesToSlot(slot) && GameplayGate()) {
        const double W = vec[0], H = vec[1];
        if (W > 0.0 && H > 0.0) {
            double boxW, boxH;
            if (W / H > gCfg.aspect) {
                boxH = H;
                boxW = H * gCfg.aspect;
            } else {
                boxW = W;
                boxH = W / gCfg.aspect;
            }
            gBoxX = (W - boxW) / 2.0;
            gBoxY = (H - boxH) / 2.0;
            gBoxW = boxW;
            gBoxH = boxH;
            vec[0] = boxW;
            vec[1] = boxH;
        }
    }
    g_origSetSize(slot, vec);
}

void HookSetPosition(void* slot, double* vec) {
    while (!g_origSetPosition) Sleep(0);
    if (AppliesToSlot(slot) && GameplayGate() && gBoxW > 0.0) {
        vec[0] = gBoxX;
        vec[1] = gBoxY;
    }
    g_origSetPosition(slot, vec);
}

}  // namespace

void InstallUIConstraint(HMODULE game) {
    LoadConfig();

    static const unsigned char kSigAttrGet[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xFF, 0x48, 0x8B, 0xD9,
        0x39, 0x79, 0x18, 0x74, 0x09, 0x48, 0x8B, 0x49};
    gAttrGet = reinterpret_cast<AttrGet_t>(FindUniquePattern(game, kSigAttrGet, sizeof(kSigAttrGet)));
    if (!gAttrGet) {
        LogLine("UI: TAttribute<FSceneViewport*>::Get not found - UI constraint disabled");
        return;
    }

    static const unsigned char kSigDpi[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20,
        0x48, 0x8B, 0xF9, 0x48, 0x81};
    InstallHook(game, "GameViewportDPIScale", kSigDpi, sizeof(kSigDpi), 15,
                reinterpret_cast<void*>(&HookDpiScale), reinterpret_cast<void**>(&g_origDpiScale));

    static const unsigned char kSigSetSize[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0x48, 0x8D, 0x51, 0x20, 0x48, 0x83,
        0xC1, 0x40, 0x4C, 0x8B, 0xC3, 0xE8, 0xD7, 0x0A};
    InstallHook(game, "CanvasSlotSetSize", kSigSetSize, sizeof(kSigSetSize), 13,
                reinterpret_cast<void*>(&HookSetSize), reinterpret_cast<void**>(&g_origSetSize));

    static const unsigned char kSigSetPos[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0x48, 0x8D, 0x51, 0x20, 0x48, 0x83,
        0xC1, 0x30, 0x4C, 0x8B, 0xC3, 0xE8, 0x03, 0x0A};
    InstallHook(game, "CanvasSlotSetPosition", kSigSetPos, sizeof(kSigSetPos), 13,
                reinterpret_cast<void*>(&HookSetPosition), reinterpret_cast<void**>(&g_origSetPosition));
}
