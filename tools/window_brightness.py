#!/usr/bin/env python3
"""Capture a window's client area and report average luminance (0..255).

Usage: python window_brightness.py <pid>
"""
import ctypes
import sys
import time
from ctypes import wintypes

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
EP = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def find_unreal_window(pid):
    found = []

    def cb(hwnd, _):
        p = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid:
            cls = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(hwnd, cls, 256)
            if cls.value == "UnrealWindow":
                found.append(hwnd)
        return True

    user32.EnumWindows(EP(cb), 0)
    return found[0] if found else None


def brightness(hwnd):
    rect = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    w, h = rect.right, rect.bottom
    if w <= 0 or h <= 0:
        return -1.0, w, h
    hdc = user32.GetWindowDC(hwnd)
    mdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    old = gdi32.SelectObject(mdc, bmp)
    PW_RENDERFULLCONTENT = 2
    user32.PrintWindow(hwnd, mdc, PW_RENDERFULLCONTENT)
    class BITMAP(ctypes.Structure):
        _fields_ = [("bmType", wintypes.LONG), ("bmWidth", wintypes.LONG), ("bmHeight", wintypes.LONG),
                    ("bmWidthBytes", wintypes.LONG), ("bmPlanes", wintypes.WORD), ("bmBitsPixel", wintypes.WORD),
                    ("bmBits", wintypes.LPVOID)]
    bm = BITMAP()
    gdi32.GetObjectW(bmp, ctypes.sizeof(bm), ctypes.byref(bm))
    bufsize = abs(bm.bmWidthBytes) * bm.bmHeight
    buf = (ctypes.c_ubyte * bufsize)()
    gdi32.GetBitmapBits(bmp, bufsize, buf)
    gdi32.SelectObject(mdc, old)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(hwnd, hdc)
    bpp = bm.bmBitsPixel // 8
    stride = bm.bmWidthBytes
    total, count = 0, 0
    step_x, step_y = max(1, w // 32), max(1, h // 32)
    for y in range(0, h, step_y):
        row = y * stride
        for x in range(0, w, step_x):
            i = row + x * bpp
            if bpp >= 3:
                b, g, r = buf[i], buf[i + 1], buf[i + 2]
            else:
                b = g = r = buf[i]
            total += (r + g + b) // 3
            count += 1
    return (total / count if count else 0.0), w, h


def main():
    pid = int(sys.argv[1])
    hwnd = find_unreal_window(pid)
    if not hwnd:
        print("no UnrealWindow")
        sys.exit(1)
    for _ in range(3):
        val, w, h = brightness(hwnd)
        print(f"brightness={val:.1f} ({w}x{h})")
        time.sleep(2)


if __name__ == "__main__":
    main()
