#pragma once
#include <windows.h>

enum class PatchResult {
    Applied,
    PatternNotFound,
    PatternNotUnique,
};

// Neutralize the aspect-ratio constraint in FViewport::CalculateViewExtents of
// the game exe identified by `gameModule`. Returns the outcome.
PatchResult ApplyUltrawidePatch(HMODULE gameModule);
