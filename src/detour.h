#pragma once
#include <windows.h>

// Minimal code-detour helper for the game exe (loaded at 0x140000000, too far
// from this DLL for rel32 jumps, so absolute mov+jmp trampolines are used).
//
// InstallHook overwrites `patchLen` bytes (must end on an instruction
// boundary, >= 13) at pattern-matched sites with `mov rax, <hook>; jmp rax`,
// and builds a trampoline per site that executes the original bytes followed
// by `mov rax, <site+patchLen>; jmp rax`. With allowMultiple every match is
// patched (for byte-identical function clones); otherwise exactly one match
// is required. `origOut` receives the last trampoline address, callable with
// the hooked function's own signature.
bool InstallHook(HMODULE gameModule, const char* name, const unsigned char* signature,
                 int signatureLen, int patchLen, void* hookFn, void** origOut,
                 bool allowMultiple = false);
