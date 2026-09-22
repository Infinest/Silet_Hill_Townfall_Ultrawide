# Townfall Super Ultrawide (32:9) Gameplay Fix

Removes the 21:9 gameplay restriction in *Silent Hill: Townfall* (Steam, UE 5.6.1)
so the full 32:9 viewport is rendered during gameplay, without stretching and
with the authored vertical FOV. Menus already supported 32:9; gameplay was
pillarboxed to 21:9.

## How it works

The game ships with full debug symbols (`Townfall-Win64-Shipping.pdb`), which
made it possible to pinpoint the exact engine functions responsible. The 21:9
clamp is enforced in **two coordinated places**, and the mod touches both:

1. **View-rect constraint** — `FViewport::CalculateViewExtents`
   (RVA `0x1798174`) shrinks the render rectangle (the black pillarbox bars)
   when the camera's aspect-ratio constraint binds. The mod flips one
   conditional jump (`jbe` → `jmp` at RVA `0x17981E2`) so the rectangle always
   stays full-size.

2. **Projection matrix** — for cameras with `bConstrainAspectRatio`, UE builds
   the projection with the camera's *authored* aspect ratio
   (`UCameraComponent::AspectRatio`, e.g. 2.35) instead of the real view-rect
   aspect: `M00 = M11 / aspectProp`. The authored-aspect frame used to land
   inside the pillarboxed rect; with the bars gone it was stretched across the
   whole 32:9 screen. A hook on
   `FMinimalViewInfo::CalculateProjectionMatrixGivenView` clears
   `bConstrainAspectRatio` (bit 0 of the flag dword at FMinimalViewInfo+0x68)
   before the matrix is built, so every camera projects with
   `M00 = M11 / rectAspect`.

Combined result at 5120x1440: full-width rendering, horizontal FOV widens
exactly by the aspect ratio, and vertical FOV stays at the authored value —
identical vertical framing to the stock 21:9 mode, without bars or stretch.
Verified in-engine: `M11 = cot(vHalf)` and `M00 = M11 / 3.5556` for all
cameras (logged via the diagnostic hook).

All patch sites are located at runtime by pattern scanning; the game exe on
disk is never modified. If a game update moves the code, the patch refuses to
apply and the game runs unmodified (see `TownfallUltraWide.log` next to the
DLL).

## Install

1. Copy `dist\dxgi.dll` into
   `E:\SteamLibrary\steamapps\common\Townfall\Townfall\Binaries\Win64\`
2. Start the game normally (through Steam).

Uninstall: delete `dxgi.dll` (and optionally `TownfallUltraWide.log` /
`TownfallUltraWide.ini`). Steam file verification is not affected - the DLL is
not part of the game manifest.

The DLL is a proxy for the system `dxgi.dll` (so the game loads it
automatically); all DXGI calls are forwarded to the real system library,
loaded by full system path.

## Config (optional)

`TownfallUltraWide.ini` next to the DLL:

```ini
[Patch]
Enabled=1      ; 0 = disable everything (game runs 100% stock)
FixMatrix=1    ; 0 = only remove the pillarbox bars (keeps authored-aspect
               ;     projection -> stretched image; useful for comparison)

[Hooks]
; Diagnostic logging hooks (all default off except CalcProj, which carries
; the FixMatrix logic). Only enable for debugging.
Enabled=1
CalcProj=1
GetCameraView=0
CalcViewExtents=0
```

## Known notes / roadmap

- Cutscene letterboxing that uses the same constraint is also lifted; pre-
  rendered FMVs keep their baked-in bars.
- **Planned (step 2, pending user confirmation):** constrain the in-game HUD
  to a centered 16:9 safe area so all UI is visible without turning your head.

## Project layout

```
build.bat            - MSVC build script -> dist\dxgi.dll
src/
  dllmain.cpp        - entry point, config, init thread
  patch.cpp/.h       - pattern scan + 1-byte patch of CalculateViewExtents
  hooks.cpp          - detour hooks (CalcProj carries the matrix fix;
                       optional diagnostic logging of FOV/rects/matrix)
  detour.cpp/.h      - absolute-jump detour helper (r11-preserving trampolines)
  log.cpp/.h         - lock-free WriteFile logger (no CRT stdio; render-thread safe)
  dxgi_exports.cpp   - proxy exports forwarded to system dxgi.dll
tools/               - reversing + test toolchain
  resolve/dump_all/layout  - DbgHelp symbol tools (uses the shipped PDB)
  disasm/annotate/find_*   - capstone disassembly + xref scanners
  steam_boot_test.py       - Steam launch + window/brightness boot tester
```

## Analysis summary (difficulty assessment)

Easy overall (a few evenings) *because the game ships a full PDB* - the
hardest part of game modding (finding the code) was reading symbols. No
anti-cheat, no integrity checks, single-player. The investigation found the
clamp is two coordinated mechanisms (rect + matrix); fixing only the first
produced a stretched image, which the in-engine diagnostic hooks (camera FOV,
camera aspect property, view rects, projection matrix entries) then nailed
down exactly.
