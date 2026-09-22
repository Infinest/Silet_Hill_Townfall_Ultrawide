#!/usr/bin/env python3
"""Find all `call rel32` sites in .text that target given RVAs of Townfall-Win64-Shipping.exe.

Usage: python find_calls.py <target_rva_hex> [more_rvas...]
"""
import sys
import struct
import pefile

EXE = r"E:\SteamLibrary\steamapps\common\Townfall\Townfall\Binaries\Win64\Townfall-Win64-Shipping.exe"
IMAGE_BASE = 0x140000000


def main():
    targets = [int(a, 16) for a in sys.argv[1:]]
    pe = pefile.PE(EXE, fast_load=True)
    with open(EXE, "rb") as f:
        data = f.read()

    for s in pe.sections:
        name = s.Name.rstrip(b"\x00").decode(errors="replace")
        if name != ".text":
            continue
        base_rva = s.VirtualAddress
        raw = data[s.PointerToRawData : s.PointerToRawData + s.SizeOfRawData]
        print(f"# scanning .text rva 0x{base_rva:X}+0x{len(raw):X}")
        i = 0
        n = len(raw) - 5
        while i < n:
            if raw[i] == 0xE8:
                rel = struct.unpack_from("<i", raw, i + 1)[0]
                site_rva = base_rva + i
                dst = IMAGE_BASE + site_rva + 5 + rel
                for t in targets:
                    if dst == IMAGE_BASE + t:
                        print(f"call -> 0x{t:X}  at rva 0x{site_rva:X}")
            i += 1


if __name__ == "__main__":
    main()
