# Editions: the EU EBOOT's addresses

Killzone HD exists as the US PlayStation Store title (NPUA80856), which the port was made from, and as the EU one
(NPEA00418). This note says how the port reaches the EU EBOOT's addresses and what is proven. Measured 2026-11-24.

## What differs

The EU EBOOT.elf is the same source, built by the same compiler and linked again after a few source edits. Of
Ghidra's 22,768 functions, 22,761 are the same instruction for instruction up to relocation; 7 changed, and the
lift's function list adds 4 more it delimits (0x221838, 0x2263F8, 0x4C7A28, 0x4CB798). The EU `.text` is 0xE8 bytes
shorter, so the code after each change moves by a growing step: 0, -8, -80, -112, -136, -148, -144, -152, -160 and
-232. The import stubs move by -0xE8, the rodata by -0x100 (one run of strings, 0x565538..0x56580C, differs), and
the data segment and bss keep their addresses: every word that differs there is a pointer into code or rodata.

## The table and KZ_GA

`re/eu/address_map.tsv` holds the step ranges, the changed rows (11 functions, 3 gaps between functions, 2 rodata
runs) and site rows for the addresses inside changed functions that the port needs. `tools/edition_map.py`:

- `check US.elf EU.elf` reads both EBOOTs and checks every word of every range outside the changed rows (in code the
  same instruction, a branch to the mapped target; elsewhere the same word or a moved pointer), the section headers,
  each site's call and each changed function's first instruction. It passed on 2026-11-24.
- `header` writes `port/kz1hd/edition.h`. Its `KZ_GA(a)` is the address `a` in the build's edition: itself in a US
  build, the table's answer when `KZ_EDITION_EU=1` (`-DKZ_EDITION=EU` in CMake). It is one constant expression, so
  `case` labels and C files use it too. An address inside a changed row without a site row gives `KZ_GA_NONE`.
- `inventory` lists every literal in `port/kz1hd`, `port/patches` and the lift's tools that may be an EBOOT address.

The port's sources keep the US addresses. Every code or rodata address `port/kz1hd` writes in code goes through
`KZ_GA` (58 of them in 15 files); data and bss addresses need none. `tools/tests/test_edition_map.py` fails on a
code or rodata literal outside `KZ_GA` (values that are not EBOOT addresses are listed in
`edition_map.NOT_ADDRESSES`) and on any `KZ_GA` argument without an EU address, compiles `KZ_GA` for both editions
against the table, and runs `check` when `KZ_EBOOT` and `KZ_EU_EBOOT` name both EBOOTs. With the US build the
touched objects were compared before and after the change: the same code and data.

The patches: none uses an EBOOT address in code. Three cite function names in comments (2005: 0x111078 and
0x110D50; 2059: 0x29BDC8); the rest of their numbers are masks, sizes, RSX IO offsets and test values.

## The addresses inside changed functions

Each one was checked with a headless Ghidra 12.1.4 script on both EBOOTs (functions created at both starts, the call
before each return address and the 25 instructions around it) and by listing every `bl` in the HUD's functions on
both sides.

| US | EU | Use | Evidence |
|---|---|---|---|
| 0x2263F8 | 0x226388 | start of an overlay function (comments; the overlay range ends at 0x227A1C) | 85 instructions and the same call target on both sides |
| 0x4C7A28 | 0x4C7988 | start of the tutorial banner (`in_tutorial`) | 981 instructions, the same 27 call targets and 54 calls; only 28 immediates differ |
| 0x4CB798 | 0x4CB6F8 | start of the HUD function (comments) | its first block, 41 instructions, is the same |
| 0x4CBBB4 | 0x4CBB14 | crosshair site | return of `bl func_00227A1C`, the 1st of 25 on both sides; the 25 instructions around it are equal |
| 0x4CD834 | 0x4CD74C | HUD text site (`full_screen_effect`) | return of `bl 0x4416C`, 1st of 3; only stack and data offsets differ around it |
| 0x4CD90C | 0x4CD824 | HUD text site | `bl 0x4416C`, 2nd of 3; the same |
| 0x4CD9D4 | 0x4CD8EC | HUD text site | `bl 0x4416C`, 3rd of 3; only a data offset differs |
| 0x4CE4BC | 0x4CE3D4 | HUD site | `bl 0x44708`, 2nd of 2; only stack and string offsets differ |
| 0x4CDC0C | 0x4CDB24 | grenade row (`hud_sites.h`) | `bl func_00227A1C`, 19th of 25; only stack offsets differ |
| 0x4CDCC0 | 0x4CDBD8 | grenade row | `bl func_00227A1C`, 20th of 25; only stack and data offsets differ |

The step alone is not enough inside a changed function: the HUD site at US 0x4CCB0C (not used by the port) would map
to 0x4CCAE4, but its EU place is 0x4CCA48 and 0x4CCAE4 is the next site's. In 0x4CB798 the EU code has 266 calls
against 269, 3 of them gone from the part that changed (0x4CC2AC..0x4CCD50). What those changes do in the game was
not looked at. The clock's double at 0x21C0F0, between two functions, is in an unchanged range (0x21C0A0).

## What the EU build still needs (stage 3)

- Function names. The lift names functions by their address, and the port calls `func_XXXXXXXX` by the US names
  (47 names in code, the `native_ppu` replacements and `PPU_LIFT_NATIVE`). The EU lift must name each EU function by
  its US address through the table, a lifter option in a new patch; every name the port uses is outside the changed
  rows except the three changed starts, which map.
- `tools/kzbuild.py` passes EBOOT addresses to the tools: setjmp 0x3B5CB0 (EU 0x3B5C10), longjmp 0x3B5DD4
  (0x3B5D34), the code end 0x52F740 (0x52F658) and the policy module 0x5B9180 (0x5B9080). `re/function_seeds.txt` is
  US: 7 seeds fall in changed functions.
- `KZ_EDITION` from the game's PARAM.SFO (TITLE_ID), `tools/check-game-files.py` and `eboot_check.h` with the EU
  EBOOT's hash, the paths that name NPUA80856, and a first lift, build and run of the EU game.
