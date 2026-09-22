#!/usr/bin/env python3
"""Launch Townfall, wait for a visible top-level window belonging to the process,
then report. Usage: python boot_test.py [--kill]"""
import ctypes
import subprocess
import sys
import time

from ctypes import wintypes

EXE = r"E:\SteamLibrary\steamapps\common\Townfall\Townfall\Binaries\Win64\Townfall-Win64-Shipping.exe"
WAIT_S = 180

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

EnumWindows = user32.EnumWindows
EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
GetWindowThreadProcessId = user32.GetWindowThreadProcessId
IsWindowVisible = user32.IsWindowVisible
GetWindowTextW = user32.GetWindowTextW
GetClassNameW = user32.GetClassNameW
GetWindowLongW = user32.GetWindowLongW
GWL_STYLE = -16
WS_POPUP = 0x80000000


def find_windows(pid):
    found = []

    def cb(hwnd, _):
        p = wintypes.DWORD()
        GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid:
            style = GetWindowLongW(hwnd, GWL_STYLE)
            title = ctypes.create_unicode_buffer(256)
            GetWindowTextW(hwnd, title, 256)
            cls = ctypes.create_unicode_buffer(256)
            GetClassNameW(hwnd, cls, 256)
            found.append((hwnd, bool(style & WS_POPUP), bool(IsWindowVisible(hwnd)),
                          title.value, cls.value))
        return True

    EnumWindows(EnumWindowsProc(cb), 0)
    return found


def main():
    kill_after = "--kill" in sys.argv
    proc = subprocess.Popen([EXE], cwd=EXE.rsplit("\\", 1)[0])
    print(f"launched pid {proc.pid}, waiting up to {WAIT_S}s for a window...")
    ok = False
    for i in range(WAIT_S):
        if proc.poll() is not None:
            print(f"PROCESS EXITED after {i}s with code {proc.returncode}")
            break
        wins = find_windows(proc.pid)
        real = [w for w in wins if not w[1]]  # non-popup windows
        if real:
            print(f"WINDOW FOUND after {i}s:")
            for w in real:
                print(f"  hwnd={w[0]:#x} visible={w[2]} title={w[3]!r} class={w[4]!r}")
            ok = True
            break
        time.sleep(1)
    if not ok and proc.poll() is None:
        print(f"no window after {WAIT_S}s (process still alive)")
    if kill_after and proc.poll() is None:
        proc.kill()
        print("killed process")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
