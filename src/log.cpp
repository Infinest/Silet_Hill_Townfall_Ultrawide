// log.cpp - Append-only diagnostic log next to the DLL.
//
// Implemented entirely on raw Win32 handles with hand-rolled formatting:
// no CRT stdio (fopen/fprintf) is used anywhere, because this logger is
// called from the game's render thread and CRT stdio locks can deadlock
// against the game's own CRT users (observed as a black-screen hang).

#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cwchar>

namespace {

HANDLE gFile = INVALID_HANDLE_VALUE;
CRITICAL_SECTION gCs;
LONG gCsInit = 0;

void EnsureInit() {
    if (InterlockedCompareExchange(&gCsInit, 1, 0) == 0) {
        InitializeCriticalSection(&gCs);
        wchar_t path[MAX_PATH];
        HMODULE self = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)&EnsureInit, &self)) {
            GetModuleFileNameW(self, path, MAX_PATH);
            wchar_t* slash = wcsrchr(path, L'\\');
            if (slash)
                wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"TownfallUltraWide.log");
        } else {
            wcscpy_s(path, L"C:\\TownfallUltraWide.log");
        }
        gFile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (gFile != INVALID_HANDLE_VALUE) SetFilePointer(gFile, 0, nullptr, FILE_END);
        gCsInit = 2;
    } else {
        while (gCsInit != 2) Sleep(0);
    }
}

char* AppendUInt(char* p, unsigned long long v) {
    char tmp[24];
    int n = 0;
    do {
        tmp[n++] = char('0' + (v % 10));
        v /= 10;
    } while (v);
    while (n) *p++ = tmp[--n];
    return p;
}

char* AppendInt(char* p, long long v) {
    if (v < 0) {
        *p++ = '-';
        v = -v;
    }
    return AppendUInt(p, (unsigned long long)v);
}

char* AppendHex(char* p, unsigned long long v) {
    char tmp[16];
    int n = 0;
    do {
        int d = int(v & 0xF);
        tmp[n++] = char(d < 10 ? '0' + d : 'A' + d - 10);
        v >>= 4;
    } while (v);
    while (n) *p++ = tmp[--n];
    return p;
}

char* AppendDouble(char* p, double d, int decimals) {
    if (d != d) {
        *p++ = 'n';
        *p++ = 'a';
        *p++ = 'n';
        return p;
    }
    if (d < 0) {
        *p++ = '-';
        d = -d;
    }
    unsigned long long scale = 1;
    for (int i = 0; i < decimals; ++i) scale *= 10;
    unsigned long long fixed = (unsigned long long)(d * (double)scale + 0.5);
    p = AppendUInt(p, fixed / scale);
    if (decimals > 0) {
        *p++ = '.';
        unsigned long long frac = fixed % scale;
        for (int i = decimals - 1; i >= 0; --i) {
            unsigned long long place = 1;
            for (int j = 0; j < i; ++j) place *= 10;
            *p++ = char('0' + (frac / place) % 10);
        }
    }
    return p;
}

int Format(char* out, size_t cap, const char* fmt, va_list ap) {
    char* p = out;
    char* end = out + cap - 1;
    while (*fmt && p < end) {
        if (*fmt != '%') {
            *p++ = *fmt++;
            continue;
        }
        ++fmt;
        int decimals = -1;
        if (*fmt == '.') {
            ++fmt;
            decimals = 0;
            while (*fmt >= '0' && *fmt <= '9') decimals = decimals * 10 + (*fmt++ - '0');
        }
        switch (*fmt) {
            case 's': {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                while (*s && p < end) *p++ = *s++;
                break;
            }
            case 'd':
            case 'i':
                p = AppendInt(p, va_arg(ap, int));
                break;
            case 'u':
                p = AppendUInt(p, va_arg(ap, unsigned));
                break;
            case 'l':
                ++fmt;
                if (*fmt == 'l') ++fmt;
                if (*fmt == 'X' || *fmt == 'x')
                    p = AppendHex(p, va_arg(ap, unsigned long long));
                else if (*fmt == 'u')
                    p = AppendUInt(p, va_arg(ap, unsigned long long));
                else
                    p = AppendInt(p, va_arg(ap, long long));
                break;
            case 'f': {
                if (decimals < 0) decimals = 2;
                p = AppendDouble(p, va_arg(ap, double), decimals);
                break;
            }
            case 'X':
            case 'x':
            case 'p':
                p = AppendHex(p, va_arg(ap, unsigned long long));
                break;
            case 'c':
                *p++ = char(va_arg(ap, int));
                break;
            case '%':
                *p++ = '%';
                break;
            default:
                *p++ = '%';
                if (*fmt) *p++ = *fmt;
                break;
        }
        if (*fmt) ++fmt;
    }
    *p = 0;
    return int(p - out);
}

}  // namespace

void LogLine(const char* fmt, ...) {
    EnsureInit();
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int len = Format(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    buf[len++] = '\n';
    EnterCriticalSection(&gCs);
    DWORD written = 0;
    WriteFile(gFile, buf, (DWORD)len, &written, nullptr);
    LeaveCriticalSection(&gCs);
}
