# Townfall Super Ultrawide (32:9) Mod

Removes the 21:9 gameplay restriction in *Silent Hill: Townfall* (Steam, UE 5.6.1)
so the full 32:9 viewport is rendered during gameplay, without stretching and
with the authored vertical FOV. Menus already supported 32:9; gameplay was
pillarboxed to 21:9.

Optionally, the in-game HUD can be constrained to a centered 16:9 (or 21:9)
safe area so all HUD elements are visible without turning your head. This
applies only during gameplay — main menu, inventory and pause screens always
use the full width.

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

**HUD constraint (step 2).** The visible HUD is Slate/UMG parented under the
viewport overlay (`SOverlay`). A vtable hook on `SOverlay::OnArrangeChildren`
(slot 71) presents the overlay's children a modified `FGeometry` — a centered
box instead of the full 32:9 rect — so every HUD widget lays itself out
inside the box without touching a single draw call. The constraint is gated to
gameplay by watching `UTownfallGameInstance`'s game-state stack (captured via
a trampoline hook on `PopGameState`): only state 4 (gameplay) gets the box.

All patch sites are located at runtime by pattern scanning; the game exe on
disk is never modified. If a game update moves the code, the patch refuses to
apply and the game runs unmodified (see `TownfallUltraWide.log` next to the
DLL).

## Install

1. Copy `dist\dxgi.dll` into
   `E:\SteamLibrary\steamapps\common\Townfall\Townfall\Binaries\Win64\`
2. Start the game normally (through Steam).

On first launch the mod creates `TownfallUltraWide.ini` with defaults next to
the DLL.

Uninstall: delete `dxgi.dll` (and optionally `TownfallUltraWide.log` /
`TownfallUltraWide.ini`). Steam file verification is not affected - the DLL is
not part of the game manifest.

The DLL is a proxy for the system `dxgi.dll` (so the game loads it
automatically); all DXGI calls are forwarded to the real system library,
loaded by full system path.

## Config (optional)

`TownfallUltraWide.ini` next to the DLL (delete it to reset to defaults):

```ini
[Camera]
; 0 - off, 1 - on
; Unlocks super ultrawide (32:9) gameplay by removing the 21:9 aspect
; cap and pillarboxing. Vertical FOV stays as authored for 16:9, so
; the image is never stretched.
Enabled=1

[UI]
; Constrain: 0 - off, the HUD spans the full screen width
;            1 - constrain the in-game HUD to a centered 16:9 box
;            2 - constrain the in-game HUD to a centered 21:9 box
; Applies only during gameplay. Main menu, inventory and pause
; screens always use the full screen width.
Constrain=1
```

## Known notes

- Cutscene letterboxing that uses the same constraint is also lifted; pre-
  rendered FMVs keep their baked-in bars.

## Project layout

```
build.bat            - MSVC build script -> dist\dxgi.dll
src/
  dllmain.cpp        - entry point, default-ini generation, config, init thread
  patch.cpp/.h       - pattern scan + 1-byte patch of CalculateViewExtents
  hooks.cpp          - camera hook (clears bConstrainAspectRatio in CalcProj)
  uiconstraint.cpp   - HUD box: SOverlay arrange-hook + gameplay state gate
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
