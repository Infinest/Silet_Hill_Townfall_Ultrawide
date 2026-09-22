// dia_layout.cpp - Dump UDT member layout from a PDB via the DIA SDK.
// Usage: dia_layout.exe <pdb> <TypeName> [maxDepth]
#include <windows.h>
#include <dia2.h>
#include <diacreate.h>

#include <cstdio>
#include <cwchar>

static void DumpType(IDiaSymbol* type, int depth, int maxDepth) {
    DWORD tag = 0;
    type->get_symTag(&tag);
    // unwrap typedefs / const / volatile / pointers / refs
    while (tag == SymTagTypedef || tag == SymTagCustomType || tag == SymTagPointerType ||
           tag == SymTagArrayType) {
        IDiaSymbol* next = nullptr;
        if (FAILED(type->get_type(&next)) || !next) return;
        type->Release();
        type = next;
        type->get_symTag(&tag);
    }
    if (tag != SymTagUDT) return;

    ULONGLONG len = 0;
    type->get_length(&len);
    if (depth == 0) wprintf(L"(type len 0x%llX)\n", len);

    IDiaEnumSymbols* children = nullptr;
    if (FAILED(type->findChildren(SymTagNull, nullptr, nsNone, &children)) || !children) return;
    IDiaSymbol* child = nullptr;
    ULONG got = 0;
    while (SUCCEEDED(children->Next(1, &child, &got)) && got == 1) {
        DWORD ctag = 0;
        child->get_symTag(&ctag);
        if (ctag == SymTagData || ctag == SymTagUDT) {
            LONG off = 0;
            child->get_offset(&off);
            BSTR name = nullptr;
            child->get_name(&name);
            ULONGLONG clen = 0;
            child->get_length(&clen);
            DWORD ckind = 0;
            child->get_dataKind(&ckind);
            const wchar_t* kind = ctag == SymTagUDT ? L"[UDT]" : L"";
            if (ctag == SymTagData)
                wprintf(L"%*s0x%04X (len 0x%llX) %s %ls\n", depth * 2 + 2, L"", (ULONG)off, clen,
                        kind, name ? name : L"?");
            else
                wprintf(L"%*s[base @0x%04X] %ls\n", depth * 2 + 2, L"", (ULONG)off,
                        name ? name : L"?");
            if (ctag == SymTagUDT && depth + 1 < maxDepth) {
                IDiaSymbol* ut = nullptr;
                if (SUCCEEDED(child->get_type(&ut)) && ut) {
                    DumpType(ut, depth + 1, maxDepth);
                    ut->Release();
                }
            }
            if (name) SysFreeString(name);
        }
        child->Release();
    }
    children->Release();
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 1;
    CoInitialize(nullptr);

    // DIA is not COM-registered on this machine: load msdia140.dll manually
    // and go through its class factory.
    HMODULE dia = LoadLibraryW(
        L"C:\\Program Files\\Microsoft Visual Studio\\18\\Enterprise\\DIA SDK\\bin\\amd64\\msdia140.dll");
    if (!dia) {
        wprintf(L"msdia140.dll load failed %lu\n", GetLastError());
        return 1;
    }
    using DllGetClassObjectFn = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
    auto getClass = reinterpret_cast<DllGetClassObjectFn>(GetProcAddress(dia, "DllGetClassObject"));
    if (!getClass) {
        wprintf(L"DllGetClassObject not found\n");
        return 1;
    }
    IClassFactory* factory = nullptr;
    HRESULT hr = getClass(CLSID_DiaSource, IID_IClassFactory, reinterpret_cast<void**>(&factory));
    if (FAILED(hr) || !factory) {
        wprintf(L"class factory failed %08X\n", hr);
        return 1;
    }
    IDiaDataSource* source = nullptr;
    hr = factory->CreateInstance(nullptr, IID_IDiaDataSource, reinterpret_cast<void**>(&source));
    factory->Release();
    if (FAILED(hr) || !source) {
        wprintf(L"CreateInstance failed %08X\n", hr);
        return 1;
    }
    if (FAILED(source->loadDataFromPdb(argv[1]))) {
        wprintf(L"loadDataFromPdb failed\n");
        return 1;
    }
    IDiaSession* session = nullptr;
    if (FAILED(source->openSession(&session)) || !session) return 1;
    if (FAILED(session->put_loadAddress(0x140000000))) return 1;

    IDiaSymbol* global = nullptr;
    session->get_globalScope(&global);

    // find the UDT by name
    IDiaEnumSymbols* enumsyms = nullptr;
    LONG disp = 0;
    // getSymbolByName wants fully qualified-ish; try findChildren on globals
    if (SUCCEEDED(global->findChildren(SymTagUDT, argv[2], nsCaseInsensitive, &enumsyms)) &&
        enumsyms) {
        IDiaSymbol* sym = nullptr;
        ULONG got = 0;
        if (SUCCEEDED(enumsyms->Next(1, &sym, &got)) && got == 1) {
            wprintf(L"== %ls ==\n", argv[2]);
            int maxDepth = argc > 3 ? _wtoi(argv[3]) : 2;
            DumpType(sym, 0, maxDepth);
            sym->Release();
        } else {
            wprintf(L"type not found: %ls\n", argv[2]);
        }
        enumsyms->Release();
    }
    global->Release();
    session->Release();
    source->Release();
    CoUninitialize();
    return 0;
}
