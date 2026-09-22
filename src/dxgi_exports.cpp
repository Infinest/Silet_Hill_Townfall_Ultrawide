// dxgi_exports.cpp - Proxy every export of dxgi.dll through to the real
// system dxgi.dll. The mod ships as dxgi.dll inside Townfall\Binaries\Win64;
// Windows loads it for the D3D12 renderer.
//
// The real library is loaded by FULL SYSTEM PATH (GetSystemDirectoryW) so the
// loader can never confuse it with this proxy. Every call is verified not to
// resolve back into this module (which would recurse), and the first few calls
// are logged for diagnosis.

#include <windows.h>

#include "log.h"

static HMODULE gReal = nullptr;

static HMODULE RealDxgi() {
    if (gReal) return gReal;
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 16) return nullptr;
    wcscat_s(path, MAX_PATH, L"\\dxgi.dll");
    HMODULE m = LoadLibraryW(path);
    if (m) {
        // Paranoia: make sure we did not get our own proxy back.
        wchar_t loaded[MAX_PATH];
        GetModuleFileNameW(m, loaded, MAX_PATH);
        if (_wcsicmp(loaded, path) != 0) {
            LogLine("FATAL: real-dxgi load returned an unexpected module");
            FreeLibrary(m);
            return nullptr;
        }
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        LogLine("dxgi proxy: real library loaded from system32");
    } else {
        LogLine("FATAL: LoadLibraryW of system dxgi failed: %lu", GetLastError());
    }
    gReal = m;
    return m;
}

static FARPROC Resolve(const char* name) {
    HMODULE real = RealDxgi();
    if (!real) return nullptr;
    FARPROC fn = GetProcAddress(real, name);
    if (!fn) return nullptr;
    // Guard against resolving back into this proxy (would recurse forever).
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)fn, &self)) {
        HMODULE us = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)&Resolve, &us);
        if (self == us) {
            LogLine("FATAL: export %s resolved back into the proxy - refusing to call", name);
            return nullptr;
        }
    }
    return fn;
}

static void TraceCall(const char* name) {
    static LONG count = 0;
    if (InterlockedIncrement(&count) <= 8) LogLine("stub called: %s", name);
}

#define DXGI_STUB(name)                                                                  \
    extern "C" __declspec(dllexport) HRESULT WINAPI name(void* a, void* b, void* c,      \
                                                         void* d) {                      \
        static FARPROC fn = Resolve(#name);                                              \
        if (!fn) return 0x80004001L; /* E_NOTIMPL */                                     \
        TraceCall(#name);                                                                \
        return reinterpret_cast<HRESULT(WINAPI*)(void*, void*, void*, void*)>(fn)(a, b, \
                                                                                   c, d); \
    }

DXGI_STUB(ApplyCompatResolutionQuirking)
DXGI_STUB(CompatString)
DXGI_STUB(CompatValue)
DXGI_STUB(CreateDXGIFactory)
DXGI_STUB(CreateDXGIFactory1)
DXGI_STUB(CreateDXGIFactory2)
DXGI_STUB(DXGID3D10CreateDevice)
DXGI_STUB(DXGID3D10CreateLayeredDevice)
DXGI_STUB(DXGID3D10GetLayeredDeviceSize)
DXGI_STUB(DXGID3D10RegisterLayers)
DXGI_STUB(DXGIDeclareAdapterRemovalSupport)
DXGI_STUB(DXGIDisableVBlankVirtualization)
DXGI_STUB(DXGIDumpJournal)
DXGI_STUB(DXGIGetDebugInterface)
DXGI_STUB(DXGIGetDebugInterface1)
DXGI_STUB(DXGIReportAdapterConfiguration)
DXGI_STUB(PIXBeginCapture)
DXGI_STUB(PIXEndCapture)
DXGI_STUB(PIXGetCaptureState)
DXGI_STUB(SetAppCompatStringPointer)
DXGI_STUB(UpdateHMDEmulationStatus)
