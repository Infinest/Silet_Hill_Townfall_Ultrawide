// hooks.cpp - camera projection hook complementing the 32:9 byte patch.
//
// FMinimalViewInfo::CalculateProjectionMatrixGivenView @ 0x1798088
//     rcx = FMinimalViewInfo, dl = EAspectRatioAxisConstraint,
//     r8 = FViewport, r9 = FSceneViewInitOptions& (ProjectionMatrix @ +0xA0)
//
// Cameras with bConstrainAspectRatio (bit 0 of the FMinimalViewInfo flag dword
// at +0x68) get a projection matrix built for the camera's authored AspectRatio
// property (M00 = M11 / aspectProp) instead of the real view rect aspect. The
// engine's matching pillarbox rect was already disabled by the
// CalculateViewExtents byte patch; without clearing this flag the
// authored-aspect image would be stretched across the full 32:9 viewport.
// Clearing the bit makes every camera project with M00 = M11 / rectAspect.

#include <windows.h>

#include <cstdint>

#include "detour.h"
#include "log.h"

namespace {

using CalcProj_t = void (*)(void* fmi, unsigned char constraint, void* viewport,
                            unsigned char* viewInit);
CalcProj_t g_origCalcProj = nullptr;

void HookCalcProj(void* fmi, unsigned char constraint, void* viewport, unsigned char* viewInit) {
    unsigned* flags = reinterpret_cast<unsigned*>(static_cast<char*>(fmi) + 0x68);
    *flags &= ~1u;
    g_origCalcProj(fmi, constraint, viewport, viewInit);
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
}
