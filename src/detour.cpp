#include "detour.h"

#include <cstdint>
#include <cstring>

#include "log.h"

namespace {

struct SectionRange {
    const unsigned char* begin;
    size_t size;
};

bool FindTextSection(HMODULE game, SectionRange& out) {
    auto* dos = reinterpret_cast<PIMAGE_DOS_HEADER>(game);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<unsigned char*>(game) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (memcmp(section->Name, ".text", 5) == 0) {
            out.begin = reinterpret_cast<const unsigned char*>(game) + section->VirtualAddress;
            out.size = section->Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

void WriteAbsoluteJump(unsigned char* dst, const void* target) {
    dst[0] = 0x48;  // mov rax, imm64
    dst[1] = 0xB8;
    uint64_t addr = reinterpret_cast<uint64_t>(target);
    memcpy(dst + 2, &addr, sizeof(addr));
    dst[10] = 0xFF;  // jmp rax
    dst[11] = 0xE0;
    dst[12] = 0x90;  // nop (13-byte form)
}

// Return jump used at the END of a trampoline. Must NOT clobber rax: the
// copied function prologue may still need it right after the jump-back
// (e.g. FViewport::CalculateViewExtents does movaps [rax-0x18], xmm6 with
// rax == rsp set by its first instruction). r11 is volatile and unused by
// those prologues.
void WriteAbsoluteJumpR11(unsigned char* dst, const void* target) {
    dst[0] = 0x49;  // mov r11, imm64
    dst[1] = 0xBB;
    uint64_t addr = reinterpret_cast<uint64_t>(target);
    memcpy(dst + 2, &addr, sizeof(addr));
    dst[10] = 0x41;  // jmp r11
    dst[11] = 0xFF;
    dst[12] = 0xE3;
}

}  // namespace

bool InstallHook(HMODULE game, const char* name, const unsigned char* signature,
                 int signatureLen, int patchLen, void* hookFn, void** origOut,
                 bool allowMultiple) {
    if (patchLen < 13 || patchLen > signatureLen) {
        LogLine("hook %s: bad parameters (patchLen=%d sigLen=%d)", name, patchLen, signatureLen);
        return false;
    }

    SectionRange text{};
    if (!FindTextSection(game, text)) {
        LogLine("hook %s: .text not found", name);
        return false;
    }

    // Find all matches up front; require exactly one unless allowMultiple.
    uintptr_t sites[16];
    int matches = 0;
    for (size_t i = 0; i + signatureLen <= text.size; ++i) {
        if (memcmp(text.begin + i, signature, signatureLen) == 0) {
            if (matches < 16) sites[matches] = reinterpret_cast<uintptr_t>(text.begin + i);
            ++matches;
        }
    }
    if (matches == 0 || (!allowMultiple && matches != 1)) {
        LogLine("hook %s: expected %d match(es), found %d", name, allowMultiple ? 16 : 1, matches);
        return false;
    }
    if (matches > 16) {
        LogLine("hook %s: too many matches (%d)", name, matches);
        return false;
    }

    for (int m = 0; m < matches; ++m) {
        const uintptr_t site = sites[m];
        // Trampoline: copied prologue + r11 jump-back. The prologue's
        // [rsp+x] stores land in the hook's outgoing-argument home space
        // (dead after the call), which has proven safe for the hooked
        // functions; deeper scratch frames collided with live locals.
        auto* trampoline = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, patchLen + 13, MEM_COMMIT | MEM_RESERVE,
                         PAGE_EXECUTE_READWRITE));
        if (!trampoline) {
            LogLine("hook %s: VirtualAlloc failed (%lu)", name, GetLastError());
            continue;
        }
        memcpy(trampoline, reinterpret_cast<void*>(site), patchLen);
        WriteAbsoluteJumpR11(trampoline + patchLen, reinterpret_cast<void*>(site + patchLen));

        auto* patch = reinterpret_cast<unsigned char*>(site);
        DWORD oldProtect = 0;
        if (!VirtualProtect(patch, patchLen, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LogLine("hook %s: VirtualProtect failed (%lu)", name, GetLastError());
            VirtualFree(trampoline, 0, MEM_RELEASE);
            continue;
        }
        WriteAbsoluteJump(patch, hookFn);
        for (int i = 13; i < patchLen; ++i) patch[i] = 0x90;  // nop padding
        VirtualProtect(patch, patchLen, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), patch, patchLen);

        *origOut = trampoline;
        LogLine("hook %s: installed at 0x%llX", name,
                (unsigned long long)(site - reinterpret_cast<uintptr_t>(game)));
    }
    return matches > 0;
}

void* FindUniquePattern(HMODULE game, const unsigned char* signature, int signatureLen) {
    SectionRange text{};
    if (!FindTextSection(game, text)) return nullptr;
    uintptr_t site = 0;
    int matches = 0;
    for (size_t i = 0; i + signatureLen <= text.size; ++i) {
        if (memcmp(text.begin + i, signature, signatureLen) == 0) {
            if (matches == 0) site = reinterpret_cast<uintptr_t>(text.begin + i);
            ++matches;
        }
    }
    if (matches != 1) {
        LogLine("FindUniquePattern: sigLen=%d matches=%d first=%llX", signatureLen, matches,
                (unsigned long long)site);
    }
    return matches == 1 ? reinterpret_cast<void*>(site) : nullptr;
}
