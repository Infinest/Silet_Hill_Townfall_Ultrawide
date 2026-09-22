// patch.cpp - Locate and neutralize FViewport::CalculateViewExtents in Townfall.
//
// Static function (UE 5.6.1, Townfall-Win64-Shipping.exe @ RVA 0x1798174):
//
//   17981D4  andps  xmm0, [abs_mask]          ; |constraintAspect - rectAspect|
//   17981DB  comiss xmm0, [epsilon=0.01f]
//   17981E2  jbe    no_adjust                 ; nearly equal -> leave rect alone
//   17981E4  comiss xmm1, [100.0f]            ; sanity bound
//   ...
//   no_adjust: copy input rect to output, return
//
// Forcing the conditional jump at +0x6E to an unconditional jump makes the
// function always return the unmodified view rectangle: the camera's
// aspect-ratio constraint (the 21:9 gameplay pillarbox) no longer applies and
// the full 32:9 viewport is rendered. Menus/UMG are unaffected (their
// constraint is a no-op already, which still is a no-op after the patch).

#include "patch.h"

#include <cstdint>
#include <cstring>

namespace {

// Wildcard = 0xFF placeholder handled by the scanner (byte 0xFF cannot appear
// in the matched sequence, so it is used as the wildcard token).
struct PatternByte {
    BYTE value;
    bool wildcard;
};

// comiss xmm0, [rip+X] ; jbe +A ; comiss xmm1, [rip+Y] ; mov edx,1 ; jbe +B
const PatternByte kPattern[] = {
    {0x0F, false}, {0x2F, false}, {0x05, false},
    {0x00, true},  {0x00, true},  {0x00, true},  {0x00, true},
    {0x76, false}, {0x00, true},               // <- +7: jbe to patch
    {0x0F, false}, {0x2F, false}, {0x0D, false},
    {0x00, true},  {0x00, true},  {0x00, true},  {0x00, true},
    {0xBA, false}, {0x01, false}, {0x00, false}, {0x00, false}, {0x00, false},
    {0x76, false}, {0x00, true},
};
constexpr size_t kPatternLen = sizeof(kPattern) / sizeof(kPattern[0]);
constexpr size_t kPatchOffset = 7;  // offset of the jbe opcode inside the pattern

bool MatchAt(const BYTE* p) {
    for (size_t i = 0; i < kPatternLen; ++i) {
        if (!kPattern[i].wildcard && p[i] != kPattern[i].value) return false;
    }
    return true;
}

}  // namespace

PatchResult ApplyUltrawidePatch(HMODULE gameModule) {
    auto* dos = reinterpret_cast<PIMAGE_DOS_HEADER>(gameModule);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return PatchResult::PatternNotFound;
    auto* nt = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<BYTE*>(gameModule) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return PatchResult::PatternNotFound;

    auto* section = IMAGE_FIRST_SECTION(nt);
    const BYTE* textBegin = nullptr;
    size_t textSize = 0;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (memcmp(section->Name, ".text", 5) == 0) {
            textBegin = reinterpret_cast<const BYTE*>(gameModule) + section->VirtualAddress;
            textSize = section->Misc.VirtualSize;
            break;
        }
    }
    if (!textBegin || textSize <= kPatternLen) return PatchResult::PatternNotFound;

    // Find all matches; require exactly one so we never patch the wrong site.
    uintptr_t site = 0;
    int matches = 0;
    for (size_t i = 0; i + kPatternLen <= textSize; ++i) {
        if (MatchAt(textBegin + i)) {
            site = reinterpret_cast<uintptr_t>(textBegin + i) + kPatchOffset;
            ++matches;
        }
    }
    if (matches == 0) return PatchResult::PatternNotFound;
    if (matches > 1) return PatchResult::PatternNotUnique;

    BYTE* patch = reinterpret_cast<BYTE*>(site);
    DWORD oldProtect = 0;
    if (!VirtualProtect(patch, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) return PatchResult::PatternNotFound;

    const BYTE original = *patch;
    if (original != 0x76) {  // jbe - sanity check
        VirtualProtect(patch, 1, oldProtect, &oldProtect);
        return PatchResult::PatternNotFound;
    }
    *patch = 0xEB;  // jmp -> always take the "leave rect unchanged" path

    VirtualProtect(patch, 1, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), patch, 1);
    return PatchResult::Applied;
}
