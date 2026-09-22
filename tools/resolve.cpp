// resolve.cpp - Resolve symbol RVAs from the shipped Townfall PDB using DbgHelp.
// Build:  cl /EHsc /O2 resolve.cpp /link /DEFAULTLIB:dbghelp
//    or:  g++ -O2 -o resolve.exe resolve.cpp -ldbghelp
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "dbghelp.lib")

static const char* kExePath =
    "E:\\SteamLibrary\\steamapps\\common\\Townfall\\Townfall\\Binaries\\Win64\\Townfall-Win64-Shipping.exe";

// Masks use wildcards; matched against undecorated names because SYMOPT_UNDNAME is set.
static const char* kMasks[] = {
    "UCameraComponent::GetCameraView*",
    "UCameraComponent::SetConstraintAspectRatio*",
    "ACineCameraComponent::GetCineCameraView*",
    "ACineCameraComponent::GetCameraView*",
    "ULocalPlayer::CalcSceneView*",
    "FSceneViewport::CalculateViewExtents*",
    "ATownfallPlayerCameraManager::Tick",
    "ATownfallPlayerCameraManager::ApplyCameraModifiers",
    "UTownfallLocalPlayer::CalcSceneView*",
    "UTownfallLocalPlayer::GetAspectRatioAxisConstraint*",
    "ULocalPlayer::GetAspectRatioAxisConstraint*",
    "UCineCameraComponent::GetCameraView*",
    "UCineCameraComponent::GetCineCameraView*",
    "UTownfallLocalPlayer::*Aspect*",
    "*CalculateViewExtents*",
    "*GetConstrainedViewport*",
    "??_7UCameraComponent*",
    "??_7ACineCameraComponent*",
};

struct Ctx {
    DWORD64 base;
};

static BOOL CALLBACK EnumSym(PSYMBOL_INFO pSym, ULONG, PVOID user) {
    Ctx* ctx = (Ctx*)user;
    // Skip inline thunk duplicates, keep functions and data.
    printf("0x%llX  %s\n", pSym->Address - ctx->base, pSym->Name);
    return TRUE;
}

int main() {
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    if (!SymInitialize(GetCurrentProcess(), NULL, FALSE)) {
        printf("SymInitialize failed: %lu\n", GetLastError());
        return 1;
    }
    DWORD64 base = SymLoadModuleEx(GetCurrentProcess(), NULL, kExePath, NULL, 0, 0, NULL, 0);
    if (!base) {
        printf("SymLoadModuleEx failed: %lu\n", GetLastError());
        return 1;
    }
    IMAGEHLP_MODULE64 mod{};
    mod.SizeOfStruct = sizeof(mod);
    if (SymGetModuleInfo64(GetCurrentProcess(), base, &mod)) {
        printf("module loaded: %s\npdb: %s\nimage size: 0x%llX\n\n", mod.ImageName,
               mod.LoadedPdbName[0] ? mod.LoadedPdbName : "(none)", mod.ImageSize);
    }

    Ctx ctx{base};
    for (const char* mask : kMasks) {
        printf("== %s ==\n", mask);
        if (!SymEnumSymbols(GetCurrentProcess(), base, mask, EnumSym, &ctx)) {
            printf("   (enum failed: %lu)\n", GetLastError());
        }
    }
    SymCleanup(GetCurrentProcess());
    return 0;
}
