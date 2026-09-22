#!/usr/bin/env python3
"""Launch Townfall via Steam, wait for window, dwell, then measure screen
brightness over time to distinguish menu (bright) from hung black screen.

Usage: python steam_boot_brightness.py [--kill]
"""
import subprocess
import sys
import time

from steam_boot_test import find_pid, windows_of
from window_brightness import find_unreal_window, brightness

WAIT_S = 150


def main():
    kill_after = "--kill" in sys.argv
    before = set(find_pid())
    subprocess.Popen(["cmd", "/c", "start", "", "steam://rungameid/1636440"])
    pid = None
    for _ in range(30):
        new = [p for p in find_pid() if p not in before]
        if new:
            pid = new[0]
            break
        time.sleep(1)
    if not pid:
        print("no process")
        sys.exit(1)
    print(f"pid {pid}")
    for i in range(WAIT_S):
        if pid not in find_pid():
            print(f"exited after {i}s")
            sys.exit(1)
        if windows_of(pid):
            print(f"window after {i}s")
            break
        time.sleep(1)

    hwnd = None
    for _ in range(10):
        hwnd = find_unreal_window(pid)
        if hwnd:
            break
        time.sleep(1)

    # sample brightness every 10s for 100s
    for t in range(0, 100, 10):
        if pid not in find_pid():
            print(f"exited at t={t}")
            break
        val, w, h = brightness(hwnd)
        print(f"t={t:3d}s brightness={val:6.1f} ({w}x{h})")
        time.sleep(10)
    if kill_after:
        subprocess.run(["powershell", "-NoProfile", "-Command", f"Stop-Process -Id {pid} -Force"],
                       capture_output=True)
        print("killed")


if __name__ == "__main__":
    main()
