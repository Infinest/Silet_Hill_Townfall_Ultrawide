// layout.cpp - Print member layout of a UDT from the Townfall PDB via DbgHelp type APIs.
// Build:  cl /EHsc /O2 layout.cpp /link dbghelp.lib
// Usage:  layout.exe <TypeName> [max_offset_hex]
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstdlib>

#pragma comment(lib, "dbghelp.lib")

// numeric fallbacks (older/newer dbghelp headers vary)
#ifndef TI_GET_CHILDRENCOUNT
#define TI_GET_SYMTAG 0
#define TI_GET_SYMNAME 1
#define TI_GET_LENGTH 2
#define TI_GET_TYPE 3
#define TI_GET_TYPEID 4
#define TI_GET_CHILDRENCOUNT 13
#define TI_GET_CHILDREN 14
#define TI_GET_OFFSET 10
#endif

static const char* kExePath =
    "E:\\SteamLibrary\\steamapps\\common\\Townfall\\Townfall\\Binaries\\Win64\\Townfall-Win64-Shipping.exe";

static HANDLE gProc;
static DWORD64 gBase;

static BOOL GetTI(ULONG id, int what, PVOID out) {
    return SymGetTypeInfo(gProc, gBase, id, (IMAGEHLP_SYMBOL_TYPE_INFO)what, out);
}

static void PrintMembers(ULONG typeIndex, ULONG depth, ULONG maxOffset) {
    DWORD tag = 0;
    GetTI(typeIndex, TI_GET_SYMTAG, &tag);
    // unwrap typedef / const / volatile / pointer / ref
    for (int guard = 0; guard < 8 && tag != 11 /*UDT*/ && tag != 18 /*BaseClass*/ && tag != 7 /*Data*/;
         guard++) {
        ULONG next = 0;
        if (!GetTI(typeIndex, TI_GET_TYPE, &next)) return;
        typeIndex = next;
        GetTI(typeIndex, TI_GET_SYMTAG, &tag);
    }
    if (tag != 11 /*UDT*/) return;

    DWORD64 len = 0;
    GetTI(typeIndex, TI_GET_LENGTH, &len);
    if (depth == 0) printf("(type len 0x%llX)\n", len);

    DWORD count = 0;
    if (!GetTI(typeIndex, TI_GET_CHILDRENCOUNT, &count)) {
        printf("(CHILDRENCOUNT failed err %lu)\n", GetLastError());
        return;
    }
    printf("(children: %lu)\n", count);
    if (count == 0) return;

    TI_FINDCHILDREN_PARAMS* params =
        (TI_FINDCHILDREN_PARAMS*)malloc(sizeof(TI_FINDCHILDREN_PARAMS) + sizeof(ULONG) * count);
    params->Count = count;
    params->Start = 0;
    if (!GetTI(typeIndex, TI_GET_CHILDREN, params)) {
        printf("(CHILDREN failed err %lu, Count now %lu)\n", GetLastError(), params->Count);
        free(params);
        return;
    }

    for (DWORD i = 0; i < params->Count; i++) {
        ULONG child = params->ChildId[i];
        DWORD ctag = 0;
        if (!GetTI(child, TI_GET_SYMTAG, &ctag)) {
            printf("(child %lu SYMTAG failed)\n", i);
            continue;
        }
        if (ctag != 18 && ctag != 7) {
            WCHAR* dbgN = nullptr;
            char dbg[256] = "?";
            if (GetTI(child, TI_GET_SYMNAME, &dbgN) && dbgN) {
                wcstombs(dbg, dbgN, sizeof(dbg) - 1);
                LocalFree(dbgN);
            }
            printf("(child %lu tag %lu %s)\n", i, ctag, dbg);
        }
        if (ctag == 18 /*BaseClass*/) {
            PrintMembers(child, depth + 1, maxOffset);
        } else if (ctag == 7 /*Data*/) {
            DWORD64 off = 0;
            GetTI(child, TI_GET_OFFSET, &off);
            if (off > maxOffset) continue;
            WCHAR* nameW = nullptr;
            char name[512] = "?";
            if (GetTI(child, TI_GET_SYMNAME, &nameW) && nameW) {
                wcstombs(name, nameW, sizeof(name) - 1);
                LocalFree(nameW);
            }
            ULONG tid = 0;
            GetTI(child, TI_GET_TYPEID, &tid);
            DWORD64 tlen = 0;
            GetTI(tid, TI_GET_LENGTH, &tlen);
            DWORD ttag = 0;
            GetTI(tid, TI_GET_SYMTAG, &ttag);
            if (ttag == 11 /*UDT*/) {
                printf("%s[UDT @0x%04llX len 0x%04llX] %s\n", depth ? "  " : "", off, tlen, name);
                if (tlen <= 0x100) PrintMembers(tid, depth + 1, maxOffset);
            } else {
                printf("  0x%04llX (len 0x%02llX) %s\n", off, tlen, name);
            }
        }
    }
    free(params);
}

static const char* gWanted = nullptr;
static ULONG gFoundType = 0;

static BOOL CALLBACK EnumTypes(PSYMBOL_INFO pSym, ULONG, PVOID) {
    if (strcmp(pSym->Name, gWanted) == 0) {
        gFoundType = pSym->TypeIndex;
        return FALSE;  // stop
    }
    return TRUE;
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    const char* typeName = argv[1];
    ULONG maxOffset = argc > 2 ? strtoul(argv[2], nullptr, 16) : 0x900;

    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    gProc = GetCurrentProcess();
    if (!SymInitialize(gProc, NULL, FALSE)) return 1;
    gBase = SymLoadModuleEx(gProc, NULL, kExePath, NULL, 0, 0, NULL, 0);
    if (!gBase) return 1;

    gWanted = typeName;
    SymEnumTypes(gProc, gBase, EnumTypes, nullptr);
    if (!gFoundType) {
        printf("type not found: %s\n", typeName);
        return 1;
    }
    printf("== %s ==\n", typeName);
    PrintMembers(gFoundType, 0, maxOffset);
    SymCleanup(gProc);
    return 0;
}
