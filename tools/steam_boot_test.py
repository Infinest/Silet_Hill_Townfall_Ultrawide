#!/usr/bin/env python3
"""Launch Townfall via Steam, wait for its window, report, optionally kill.

Usage: python steam_boot_test.py [--kill]
"""
import ctypes
import subprocess
import sys
import time
from ctypes import wintypes

class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)),
        ("th32ModuleID", wintypes.DWORD),
        ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD),
        ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", wintypes.DWORD),
        ("szExeFile", ctypes.c_wchar * 260),
    ]

WAIT_S = 150
PROC = "Townfall-Win64-Shipping"

user32 = ctypes.windll.user32
EP = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def find_pid():
    out = []
    h = ctypes.windll.kernel32.CreateToolhelp32Snapshot(0x2, 0)
    pe = PROCESSENTRY32W()
    pe.dwSize = ctypes.sizeof(pe)
    if ctypes.windll.kernel32.Process32FirstW(h, ctypes.byref(pe)):
        while True:
            if pe.szExeFile == PROC + ".exe":
                out.append(pe.th32ProcessID)
            if not ctypes.windll.kernel32.Process32NextW(h, ctypes.byref(pe)):
                break
    ctypes.windll.kernel32.CloseHandle(h)
    return out


def windows_of(pid):
    found = []

    def cb(hwnd, _):
        p = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid:
            cls = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(hwnd, cls, 256)
            if cls.value == "UnrealWindow" and user32.IsWindowVisible(hwnd):
                found.append(hwnd)
        return True

    user32.EnumWindows(EP(cb), 0)
    return found


def main():
    kill_after = "--kill" in sys.argv
    before = set(find_pid())
    subprocess.Popen(["cmd", "/c", "start", "", "steam://rungameid/1636440"])
    print("steam launch sent, waiting for process...")
    pid = None
    for _ in range(30):
        new = [p for p in find_pid() if p not in before]
        if new:
            pid = new[0]
            break
        time.sleep(1)
    if not pid:
        print("no new process appeared")
        sys.exit(1)
    print(f"game pid {pid}")
    ok = False
    for i in range(WAIT_S):
        if not find_pid() or pid not in find_pid():
            print(f"process exited after {i}s")
            break
        if windows_of(pid):
            print(f"WINDOW after {i}s, dwelling 25s...")
            time.sleep(75)
            if pid in find_pid():
                print("still alive after dwell: STABLE")
                ok = True
            else:
                print("CRASHED during dwell")
            break
        time.sleep(1)
    if not ok and pid in find_pid():
        print(f"no window after {WAIT_S}s (still running)")
    if kill_after:
        subprocess.run(["powershell", "-NoProfile", "-Command",
                        f"Stop-Process -Id {pid} -Force"], capture_output=True)
        print("killed")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
