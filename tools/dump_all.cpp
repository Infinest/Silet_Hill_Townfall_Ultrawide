// dump_all.cpp - Dump every symbol RVA + name from the Townfall PDB to symbols.txt.
// Build:  cl /EHsc /O2 dump_all.cpp /link dbghelp.lib
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>

#pragma comment(lib, "dbghelp.lib")

static const char* kExePath =
    "E:\\SteamLibrary\\steamapps\\common\\Townfall\\Townfall\\Binaries\\Win64\\Townfall-Win64-Shipping.exe";

static FILE* gOut;
static DWORD64 gBase;

static BOOL CALLBACK EnumSym(PSYMBOL_INFO pSym, ULONG, PVOID) {
    if (pSym->Address >= gBase && pSym->Address < gBase + 0xA978000)
        fprintf(gOut, "0x%llX\t%s\n", pSym->Address - gBase, pSym->Name);
    return TRUE;
}

int main() {
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    if (!SymInitialize(GetCurrentProcess(), NULL, FALSE)) return 1;
    gBase = SymLoadModuleEx(GetCurrentProcess(), NULL, kExePath, NULL, 0, 0, NULL, 0);
    if (!gBase) return 1;
    fopen_s(&gOut, "symbols.txt", "w");
    if (!SymEnumSymbols(GetCurrentProcess(), gBase, "*", EnumSym, nullptr))
        printf("enum failed: %lu\n", GetLastError());
    fclose(gOut);
    printf("done\n");
    SymCleanup(GetCurrentProcess());
    return 0;
}
