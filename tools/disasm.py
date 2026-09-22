#!/usr/bin/env python3
"""Disassemble a range of Townfall-Win64-Shipping.exe by RVA, using capstone.

Usage: python disasm.py <rva_hex> <length_hex> [--raw]
Prints RVA + bytes + Intel-syntax disassembly for the .text range.
"""
import sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

EXE = r"E:\SteamLibrary\steamapps\common\Townfall\Townfall\Binaries\Win64\Townfall-Win64-Shipping.exe"


def main():
    rva = int(sys.argv[1], 16)
    length = int(sys.argv[2], 16)

    pe = pefile.PE(EXE, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])

    with open(EXE, "rb") as f:
        data = f.read()

    def rva_to_off(addr):
        for s in pe.sections:
            va = s.VirtualAddress
            size = max(s.Misc_VirtualSize, s.SizeOfRawData)
            if va <= addr < va + size:
                return s.PointerToRawData + (addr - va)
        raise ValueError(f"RVA 0x{addr:X} not in any section")

    off = rva_to_off(rva)
    code = data[off : off + length]

    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = False
    for insn in md.disasm(code, rva + pe.OPTIONAL_HEADER.ImageBase):
        b = " ".join(f"{x:02X}" for x in insn.bytes)
        print(f"{insn.address - pe.OPTIONAL_HEADER.ImageBase:>10X}  {b:<24} {insn.mnemonic:<8} {insn.op_str}")


if __name__ == "__main__":
    main()
