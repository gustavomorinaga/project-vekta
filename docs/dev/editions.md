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
  `PPU_LIFT_NATIVE` resolve; an EU address with no US one would be `func_ADDR_eu` (none in this lift). One line
  per function start that moved: 21,831 of the 23,363. The pieces and call targets the lifter adds outside the list
  keep their EU names; the lifter stops if one of those would take a name the file gave (none did);
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
  crosshair and the German tutorial banner with its keyboard glyph.
- 2026-11-24/25, the same machine, after the names file was cut to one line per moved function (the lift's only
  difference: 215 pieces outside the function list keep EU names): every campaign level loads on the EU build
  (`KZ_UNLOCK_LEVELS=1`, `KZ_DEBUG_GOD=1`, a fresh profile, the unlocked list opening on level 11 and UP x (11 - N)
  for level N; one run per level, 140 to 200 s, frames every 5 s). Levels 1, 2, 5, 6, 7, 8 and 10 open their own
  `Movies/Level N/Intro.avi`; 3, 4 and 9 have none; each reached play with its HUD, no `[kz-crash]` line. Level 11
  (La Speranza) ran with `PS3_LANG=it`: the Italian front end (Opzioni, Gioco, Seleziona livello, Seleziona
  difficoltà, Seleziona personaggio). Level 1's start set beside a PS3 longplay of the game (local reference
  video): the same intro movie, loading screen, trench, palette and HUD (health bar, 270 rounds); the player stood
  still in these runs, so only the opening view was compared. The US build of this branch (its own lift and build,
  `KZ_EDITION=US`) still boots to Level 1 from a fresh profile (title id from the VFS's PARAM.SFO, the Level 1 movie
  and play). Not checked: Spanish, French, Dutch, saves loaded back, play past the start of a level, Windows.

## The installer and the launchers

"Use my RPCS3 install" (`tools/release/rpcs3_import.py`) looks for NPUA80856, then NPEA00418, in the player's RPCS3
(`TITLES`), copies the licence of the one it found and checks the decrypted files against that edition's sizes and
hashes; its results add `title=`. The Windows setup (`installer/vekta.iss`) takes a game folder whose PARAM.SFO says
either title. The disc route (`tools/release/rpcs3_disc.py`) installs only the US disc's package: an EU package
(content ID `EP9000-NPEA00418_00-GKILLZONE1000001`, the Store's) is named and sent to "Use my RPCS3 install"
(`eu_pkg`, exit code 71).

The European disc (BCES-01743, measured 2026-11-25) holds the game itself, not a package: a folder copy of it has
the European edition's program and archive, byte for byte (sizes and SHA-256 in
[GETTING-FILES.md](../GETTING-FILES.md)). The disc route takes that folder (`find_eu_disc`, `prepare_eu_disc`):
nothing is installed, the official RPCS3 decrypts a copy of `EBOOT.BIN` into `work/prep`, and the archive is read
where it is. A copy whose files are still encrypted as on the disc is named and refused (`eu_disc_raw`), and one
with other files too (`eu_disc_files`). Its FIOS cache folder is named by its title id, BCES01743.

The release notes (`tools/release/release_notes.py`) and `installer/recomplauncher.json` (the RecompLauncher
manifest) name both editions since 0.8.0: the game box takes either edition's installed folder or a disc copy, and
`dumps` lists each edition's decrypted `EBOOT.elf`.

## What the EU edition still needs

- Play past each level's start, saves loaded back, and its Dutch, French and Spanish (not checked).
- What the 11 changed functions do differently in the game was not looked at.
