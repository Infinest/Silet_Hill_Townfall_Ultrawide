#!/usr/bin/env python3
"""Find rip-relative references to given RVA targets in .text (inlined-copies detector).

Usage: python find_refs.py <target_rva_hex> [more...]
Reports the instruction-start RVA of each reference (opcode must be andps/comiss/mulss/divss/addss/movss).
"""
import sys
import struct
import pefile

EXE = r"E:\SteamLibrary\steamapps\common\Townfall\Townfall\Binaries\Win64\Townfall-Win64-Shipping.exe"
IMAGE_BASE = 0x140000000

# opcodes that end with a rip-relative disp32 as the LAST 4 bytes of the instruction
SUFFIX_OPS = {
    0x0F54,  # andps
    0x0F2F,  # comiss
    0xF30F59,  # mulss (prefixed)
    0xF30F5E,  # divss
    0xF30F58,  # addss
    0xF30F10,  # movss load
    0xF30F11,  # movss store
}


def main():
    targets = set(int(a, 16) for a in sys.argv[1:])
    pe = pefile.PE(EXE, fast_load=True)
    with open(EXE, "rb") as f:
        data = f.read()

    def rva_to_off(addr):
        for s in pe.sections:
            va = s.VirtualAddress
            size = max(s.Misc_VirtualSize, s.SizeOfRawData)
            if va <= addr < va + size:
                return s.PointerToRawData + (addr - va)
        return None

    for s in pe.sections:
        name = s.Name.rstrip(b"\x00").decode(errors="replace")
        if name != ".text":
            continue
        base_rva = s.VirtualAddress
        raw = data[s.PointerToRawData : s.PointerToRawData + s.SizeOfRawData]
        # .rdata values for context
        print(f"# .text scan; targets: {[hex(t) for t in sorted(targets)]}")
        for t in sorted(targets):
            off = rva_to_off(t)
            if off is not None:
                print(f"# const @ rva 0x{t:X}: {data[off:off+16].hex()}")

        i = 0
        n = len(raw) - 8
        while i < n:
            # candidate instruction encodings of interest, disp32 at end
            matched = None
            insn_len = 0
            b0 = raw[i]
            if b0 == 0x0F and raw[i + 1] in (0x54, 0x2F) and (raw[i + 2] >> 6) == 0 and (raw[i + 2] & 0xC7) == 0x05:
                matched = raw[i] << 8 | raw[i + 1]
                insn_len = 7
            elif b0 == 0xF3 and raw[i + 1] == 0x0F and raw[i + 2] in (0x59, 0x5E, 0x58, 0x10, 0x11) and (raw[i + 3] >> 6) == 0 and (raw[i + 3] & 0xC7) == 0x05:
                matched = raw[i] << 16 | raw[i + 1] << 8 | raw[i + 2]
                insn_len = 8
            if matched in SUFFIX_OPS:
                disp = struct.unpack_from("<i", raw, i + insn_len - 4)[0]
                site_rva = base_rva + i
                dst = IMAGE_BASE + site_rva + insn_len + disp
                if dst - IMAGE_BASE in targets:
                    print(f"ref -> 0x{dst - IMAGE_BASE:X}  at rva 0x{site_rva:X}  ({matched:X})")
            i += 1


if __name__ == "__main__":
    main()
