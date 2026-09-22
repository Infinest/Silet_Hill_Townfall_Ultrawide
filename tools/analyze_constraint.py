#!/usr/bin/env python3
"""Detect inlined copies of FViewport::CalculateViewExtents in Townfall-Win64-Shipping.exe.

Signature per copy:
  andps xmm, [abs_mask@0x8A93EA0]
  comiss xmm, [eps@0x8A93EC0]      (+7..+0x14 after the andps)
  comiss xmm, [100.0f@0x8A93E80]   (+0x10..+0x24 after the andps)
Optionally followed (within 0x60 bytes) by the rounding idioms
  cvtss2si eax,xmm4; sar eax,1  and  cvtss2si ecx,xmm3; sar ecx,1
"""
import pefile

EXE = r"E:\SteamLibrary\steamapps\common\Townfall\Townfall\Binaries\Win64\Townfall-Win64-Shipping.exe"
IMAGE_BASE = 0x140000000
MASK, EPS, HUNDRED = 0x8A93EA0, 0x8A93EC0, 0x8A93E80


def rip_refs(raw, base, opfilter):
    """Yield (rva, target) for rip-relative disp32 instructions matching opfilter(raw, i) -> insn_len."""
    i, n = 0, len(raw) - 8
    while i < n:
        L = opfilter(raw, i)
        if L:
            import struct
            disp = struct.unpack_from("<i", raw, i + L - 4)[0]
            yield base + i, base + i + L + disp
        i += 1


def f54_or_f2f(raw, i):
    if raw[i] == 0x0F and raw[i + 1] in (0x54, 0x2F) and (raw[i + 2] >> 6) == 0 and (raw[i + 2] & 0xC7) == 0x05:
        return 7
    return 0


def main():
    pe = pefile.PE(EXE, fast_load=True)
    data = open(EXE, "rb").read()
    s = [x for x in pe.sections if x.Name.rstrip(bytes([0])) == b".text"][0]
    raw = data[s.PointerToRawData : s.PointerToRawData + s.SizeOfRawData]
    base = s.VirtualAddress

    mask_sites, eps_sites, hundred_sites = [], [], []
    for rva, tgt in rip_refs(raw, base, f54_or_f2f):
        if tgt == MASK:
            mask_sites.append(rva)
        elif tgt == EPS:
            eps_sites.append(rva)
        elif tgt == HUNDRED:
            hundred_sites.append(rva)

    print(f"mask refs: {len(mask_sites)}, eps refs: {len(eps_sites)}, 100f refs: {len(hundred_sites)}")
    hits = []
    for m in mask_sites:
        e = next((x for x in eps_sites if m + 7 <= x <= m + 0x14), None)
        h = next((x for x in hundred_sites if m + 0x10 <= x <= m + 0x24), None)
        if e and h:
            hits.append(m)
    print("CalculateViewExtents inline candidates:")
    for m in hits:
        print(f"  andps site rva 0x{m:X}")


if __name__ == "__main__":
    main()
