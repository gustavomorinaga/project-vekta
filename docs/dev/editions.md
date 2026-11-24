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

## The EU lift and build

`tools/kzbuild.py` takes the edition from the game folder's PARAM.SFO (TITLE_ID NPUA80856 or BCUS98116: US;
NPEA00418 or BCES01743: EU; `KZ_EDITION` overrides it). For the EU edition the lift:

- maps the US seeds of `re/function_seeds.txt` through the table (the 7 that fall in changed functions are those
  functions' starts, which map);
- passes the EU setjmp 0x3B5C10, longjmp 0x3B5D34, code end 0x52F658 and policy module 0x5B9080;
- writes `work/lift/analysis/func_names.txt` (`edition_map.func_names`) and gives it to the lifter's `--func-names`
  (patch 3038): each EU function keeps its US name, so the port's `func_` names, the `native_ppu` replacements and
  `PPU_LIFT_NATIVE` resolve; an EU address with no US one is `func_ADDR_eu`. Every EU address whose plain name
  would be another function's US name is named too, so the file is large (about 1.2 million lines); the lifter
  stops if two addresses would still share a name;
- writes `KZ_EBOOT_EDITION_EU` into `gen/eboot_id.h`; the build passes `-DKZ_EDITION` from it, and `main.cpp` stops
  the compile if the two differ.

`tools/check-game-files.py` and the `data` step check each edition's own sizes and hashes
(`edition_map.EDITIONS`). The save folder is named by the title id, as on the PS3: `savedata/NPEA00418` for the
EU edition (the game asked for that folder on the first run).

Checked 2026-11-24 on Linux (RTX 3070 Ti):

- EU lift: 23,363 functions found, the same count as the US lift's list; all 67 `func_` names the port cites are
  declared. Build: KZ1HD links.
- US output unchanged: two US lifts, one with release/v0.8.0's tools and patched tree and one with this branch's,
  give the same `gen/`, `spu/` and `analysis/` (no line of difference).
- Game runs, headless, under the bench lock, frames dumped every 3 or 5 s: (1) 75 s from a fresh profile, no input:
  the health warning, logos and intro movie, save folder NPEA00418 created; (2) 170 s with `PS3_LANG=de` and the
  `PAD_SCRIPT` of docs/dev/testing.md to Level 1: the German front end (Optionen, Spiel, Extras; Level wählen,
  Schwierigkeitsgrad, Charakter auswählen), the Level 1 intro movie and about 60 s of Level 1 with the HUD, the
  crosshair and the German tutorial banner with its keyboard glyph. Not checked: other languages, other levels,
  saves loaded back, Windows.

## What the EU edition still needs

- The installer and launcher know only NPUA80856: `tools/release/rpcs3_import.py`, `rpcs3_disc.py`, `firstrun.py`
  and the `KZ_EXIT_WRONG_EDITION` text name it. `docs/GETTING-FILES.md` and the release notes too.
- What the 11 changed functions do differently in the game was not looked at.
