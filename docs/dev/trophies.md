# Trophies: where each one is unlocked

Audit of Killzone HD's 59 trophies (NPWR03819, US EBOOT NPUA80856), 2026-11-26. Every unlock goes through one
engine function and one system call, so the HLE in `libs/network/sceNpTrophy.c` (patches 5001, 5002) sees all of
them. Names below are the engine's own spelling (the strings in the EBOOT), which is what the scripts pass; the
TRP's spelling differs in a few places and is never used for the lookup.

How it was measured: `work/trophy_audit.py` (not in the repository; it reads the player's EBOOT) finds every
`bl 0x1D2518` and the name string handed to the string constructor `0x1479C` before it; the 21 Lua calls come
from the mission scripts in `data_ps3.psarc` (`tools/psarc_ls.py`, `tools/lua4dis.py --src`); grades and the
platinum link from `TROPCONF.SFM`. Disassembly with `llvm-objdump --triple=powerpc64`.

## The engine side

- `TrophySystem` (Lua `gTrophySystem`): `+0x08` state (4 ready, 3 dropped or in error, else pending),
  `+0x14` unlock cache, `+0x28..+0x30` id queue, `+0x4C/+0x54` names queued before the system is ready,
  `+0x58` enabled (1 from the constructor `func_001D1C20`; 0 makes every unlock a no-op).
- `func_001D2518` unlocks by name: 41 call sites, 39 in engine code, 1 the Lua binding (`func_004A0DC8`,
  call at `0x4A0DF8`) and 1 internal: `func_001D2760` replays the names queued before the system was ready, once
  the init thread (`func_001D2D24`) has moved the state to 4.
- `func_001D33D8` resolves a name: a linear search of 59 entries of 20 bytes at `+0x14`, each a hashed string
  object built from the EBOOT's own name table (the 59 names in id order, next to the `TrophySystem` strings),
  compared with `func_0001A350` (hash and length, then the bytes). The match is exact, but both sides are the
  EBOOT's spelling, so `'Supply And Demand'` (script) finds id 38 (`Supply and Demand` in the TRP): seen unlocked
  on the maintainer's machine on 2026-11-11.
- `func_001D2E48`, the unlock thread, is the only caller of `sceNpTrophyUnlockTrophy`. `func_001D296C` sets
  `0x828724` to 1 and aborts the handle; the status callback `func_001D2CE4` then returns -1 for statuses 1 to 4.
- The Lua `SaveDataDisabledTrophies` (`0x4A0E48`) returns 0 and does nothing.

## Timed trophies and the tick rate

Brouhaha (`func_001C04C8`) keeps the times of the last 30 kills and unlocks when the newest minus the oldest is
under 60.0 (float at `0x1C04C4`); 4 X 4 X 4 (`func_001C0620`, Chimera kills only) keeps 4 and compares with 4.0
(`0x1C0618`). Both take the time from `func_002160A0`, the float at `0x130CF24`. ASSUMPTION: that float is the
game clock's time in seconds. `KZ_TICK_HZ` (`port/kz1hd/main.cpp`, `kz_tick_rate`) changes the ticks per second
and the seconds per tick together, so a second of game time stays a second and the windows do not change at 60
or 144 Hz. Brouhaha and 4 X 4 X 4 were both unlocked on the maintainer's machine (2026-11-08, 30 Hz clock).

## Chapter 5, "Escape" (id 45)

The only unlock is in `Mission.5.Docks2`: when the player is in the end area, alive, and the squads `HGH34a`,
`HGH35a` and `HGH35b` are dead, the script marks `Escape_Docks` succeeded, sets `gIsFinished`, stops the stream
and then calls `UnlockTrophy('Escape')`. `Escape_Docks` is a primary objective, and the generic end check
(`EndByObjectiveStatus`) only wins the level when every primary objective succeeded, so a normal finish of
Docks2 always passes through that call. Unlike the other chapters, the script does not set `STATUS_WON` itself
before the unlock (the end check does it on the next update). Open: 44 and 46 were unlocked on 2026-11-08 and
2026-11-11, 45 never; no log of a Docks2 finish exists. The likely cause is that Docks2 was not finished in a
normal run (level select or a skip), not the code; to prove: finish Docks2 with `KZ_SCRIPT_LOG=1` and look for
the `Objective Complete - Exit Level` trace and the unlock line.

## The table

Grade: P platinum, G gold, S silver, B bronze. Source: a Lua mission script, or the engine function and the
address after its `bl 0x1D2518`. Seen: unlocked in the maintainer's store on 2026-11-26. Prove: how to get it
for a check (play = play the condition; the chapter trophies by finishing that chapter's last level).

| id | grade | name | source | condition | seen | prove |
|---|---|---|---|---|---|---|
| 0 | P | Platinum | the HLE, with the last of 1..58 | every other trophy | no | the host test |
| 1 | B | Captain | `func_002956E0` 0x295CF0 | the campaign finished on easy | no | play |
| 2 | S | Colonel | `func_002956E0` 0x295CC0 | the campaign finished on normal | no | play |
| 3 | G | General | `func_002956E0` 0x2961B4 | the campaign finished on hard | no | play |
| 4 | B | Toothpick Master | `func_001B4630` 0x1B4E90 | 20 knife kills | yes | play |
| 5 | B | Backstabber | `func_001B4630` 0x1B4F1C | 20 kills with the FSK-7 | no | play |
| 6 | B | Bullheaded | `func_001B4630` 0x1B48DC | 20 headshots with the M4 pistol | no | play |
| 7 | B | Tripletapper | `func_001B4630` 0x1B4D2C | 3 headshots in one Tropov magazine, secondary fire | no | play |
| 8 | B | 3 Birds With One Stone | `func_001C0F54` 0x1C11D4 | 3 kills with one cooked grenade | yes | play |
| 9 | B | One For You And One For You | `func_001B4630` 0x1B4DFC | 2 kills with one M13 double shot | yes | play |
| 10 | B | Disproportionate Force | `func_001B5C48` 0x1B5EA0 | one soldier killed by 3 Hadra rockets | no | play |
| 11 | B | Eco-Warrior | `func_001B4630` 0x1B49A8 | 15 hits without reloading, M66 secondary fire | no | play |
| 12 | B | Call Of The Siren | `func_001B4630` 0x1B4B68 | 25 Siren kills | yes | play |
| 13 | B | Explosive Consequences | `func_001C0F54` 0x1C122C | 5 kills with one M82-G grenade | no | play |
| 14 | S | Scavenger | `func_002E3B30` 0x2E4258 | 10,000 LAR rounds picked up | no | play (long) |
| 15 | S | 4 X 4 X 4 | `func_001C0620` 0x1C0760 | 4 Chimera kills within 4 s | yes | play |
| 16 | B | Right in the Jewels | `func_001B4630` 0x1B4B18 | 20 melee kills with the M224-A3 | no | play |
| 17 | B | Designation: Demolition | `func_001B5C48` 0x1B62B4, `func_001DEF80` 0x1DF104 | 3 towers destroyed with the Dohvat | no | play |
| 18 | B | The Reds Of Their Eyes | `func_001B4630` 0x1B4CD0 | 10 sniper headshots | yes | play |
| 19 | B | 2 For The Price Of 1 | `func_001C0F54` 0x1C1284 | 2 kills with one Pup shot | yes | play |
| 20 | B | Beep Beep Beeeep | `func_001B4630` 0x1B4C80 | 10 kills with the M327's secondary fire | no | play |
| 21 | B | Get out of my comfort zone! | `func_001B5C48` 0x1B5DA0 | a MAW kill within 1 m, surviving it | no | play |
| 22 | B | Artillery Artist | `func_001B4630` 0x1B4D94 | a Siska kill from 100 m or more | no | play |
| 23 | B | Covering Fire | `func_001B4630` 0x1B4C0C | 10 kills on the M224 mounted gun | yes | play |
| 24 | B | Something Borrowed | `func_001B4630` 0x1B4BB8 | 20 kills on the Scylla mounted gun | no | play |
| 25 | B | Short Controlled Bursts | `func_000106D8` 0x10A84 | a level finished above 50 % accuracy | yes | play |
| 26 | B | Economical Soldier | `func_000106D8` 0x10E24 | a level finished above 75 % accuracy | yes | play |
| 27 | S | Nobody Hides From Me | `func_000106D8` 0x10E5C | a level finished above 90 % accuracy | yes | play |
| 28 | B | 1 v 50 | `func_001B4630` 0x1B4868 | 50 kills without dying | yes | play |
| 29 | B | Brouhaha | `func_001C04C8` 0x1C0608 | 30 kills within 60 s | yes | play |
| 30 | B | Burn Baby Burn | `func_002D0158` 0x2D0238 | the first APC destroyed | yes | play |
| 31 | B | Tanks For The Good Times | Lua `Mission.1.Industries` | both tanks destroyed in Industries | yes | play |
| 32 | B | Elite? HA! | `func_001B4630` 0x1B493C | the first 2 Elite killed | yes | play |
| 33 | B | That Got Their Attention | Lua `Mission.6.Firebase` | the missile APCs destroyed | yes | play |
| 34 | B | They're On Our Side Now | Lua `Mission.6.Firebase`, `Mission.9.AlpineValley` | sentry bots turned, as Hakha | yes | play |
| 35 | B | Anti-Air Specialist | Lua `Mission.6.Firebase` | 2 dropships brought down | yes | play |
| 36 | B | Bounty Hunted | Lua `Mission.7.3` | the bounty hunter killed | yes | play |
| 37 | B | Abridged Bridge | Lua `Mission.8.JungleValley` | the bridge brought down | yes | play |
| 38 | B | Supply And Demand | Lua `Mission.8.ForwardLogisticsBase` | the ammo dumps blown up | yes | play |
| 39 | B | Generally Better | Lua `Mission.9.Underground` | Lente and his guards killed | yes | play |
| 40 | B | Orbital Strike | Lua `Mission.11.ControlRoom` | Adams killed by the player with a hand grenade | no | play |
| 41 | B | Helghast Assault | Lua `Mission.1.HQGrounds` | chapter 1 finished | yes | chapter end |
| 42 | B | Vekta Evacuates | Lua `Mission.2.Shaft` | chapter 2 finished | yes | chapter end |
| 43 | B | New Allies | Lua `Mission.3.Park2` | chapter 3 finished | yes | chapter end |
| 44 | B | Strange Company | Lua `Mission.4.Street2` | chapter 4 finished | yes | chapter end |
| 45 | B | Escape | Lua `Mission.5.Docks2` | chapter 5 finished, the last 3 squads dead | no | chapter end (see above) |
| 46 | B | Misty Waters | Lua `Mission.6.Approach` | chapter 6 finished | yes | chapter end |
| 47 | B | Hunting the Traitor | Lua `Mission.7.3` | chapter 7 finished | yes | chapter end |
| 48 | B | Forging a Path | Lua `Mission.8.ForwardLogisticsBase` | chapter 8 finished | yes | chapter end |
| 49 | B | Hidden Pasts | Lua `Mission.9.Underground` | chapter 9 finished | yes | chapter end |
| 50 | B | Onwards And Upwards | Lua `Mission.10.WeatherStation` | chapter 10 finished | yes | chapter end |
| 51 | B | Hope | Lua `Mission.11.ControlRoom` | chapter 11 finished | yes | chapter end |
| 52 | B | Rapid Reaction Force | `func_002956E0` 0x295C84 | chapters 1 to 11 as Templar | no | play |
| 53 | B | Shadow Marshal | `func_002956E0` 0x295BB0 | chapters 3 to 11 as Luger | no | play |
| 54 | B | ISA Regulator | `func_002956E0` 0x295DD8 | chapters 4 to 11 as Rico | no | play |
| 55 | B | Field Operative | `func_002956E0` 0x295D6C | chapters 5 to 11 as Hakha | no | play |
| 56 | B | Helghast Champion | `func_000106D8` 0x10F8C | a Battlefields win as Helghast, AI on hard | no | play (offline bots) |
| 57 | B | ISA Saviour | `func_000106D8` 0x10F40 | a Battlefields win as ISA, AI on hard | no | play (offline bots) |
| 58 | B | Two's Company | `func_000106D8` 0x10DF8 | a Battlefields win in split screen | no | two pads |

33 of 58 seen (not counting the platinum). Engine sites: 39 for 38 trophies (Designation: Demolition has two);
Lua: 21 calls for 20 trophies (They're On Our Side Now has two).

## Where a toast can be drawn (stage 2)

Spike, 2026-11-26, read only (no prototype built); built as the copy, see the next section. In `libs/video/rsx_vulkan_backend.c`,
`vk_present_window_body` records the window's command buffer: the display image (DLSS and FSR 3.1 have already
written it at the end of the scene, `rsx_dlss_evaluate`/`rsx_fsr3_evaluate`), then FSR 1 (EASU + RCAS) into
`s_fsr.out` when it is on, then the clear of the bars and the blit (linear, or nearest from the FSR output) into
`s_vk.sc_images[idx]`. The hook is right after that blit loop and before the `PS3RECOMP_VK_DUMP_PRESENT` read-back
and the barrier to `PRESENT_SRC`: every upscaler path has run, the image is the window's size, it is in
`TRANSFER_DST_OPTIMAL`, and a dumped frame would show the toast too. The game's frame and its HUD are never touched.

Two ways to draw there:

- A copy (`vkCmdCopyBufferToImage` from a host-visible buffer the port fills once per toast): no pipeline, no
  change to the swap chain, cost a copy of the toast's pixels (about 150 KB for 400 x 96) on the frames it shows.
  No blending: the card is opaque, its corners drawn against a fixed dark colour. The swap chain's format is
  usually B8G8R8A8, so the port writes BGRA.
- A blended quad: the swap chain needs `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT` (it has only `TRANSFER_DST`, plus
  `TRANSFER_SRC` for the dump), a render pass with `LOAD_OP_LOAD`, a framebuffer per swap-chain image rebuilt with
  it, and one textured-quad pipeline. A few hundred lines; a draw of a few thousand pixels.

The copy was the first step. The blend that followed (patch 2098) is neither: a compute pass on buffers, see below.

## The notice (stage 2, 2026-11-26)

Built as the copy above: ps3recomp patch 2097 (`rsx_vulkan_set_overlay`) calls the port once per present on the
presenting thread (the window's own thread, patch 2090) and puts the rects it returns over the window's image after
the blit. Patch 2098 blends them (premultiplied alpha): the pixels under the rects are copied out of the swap chain
image into a host buffer in the overlay's layout, one compute dispatch (`vulkan/rsx_vk_overlay.comp`) blends there,
and they are copied back, so the swap chain needs only `TRANSFER_SRC` (asked whenever the surface allows; without it
the pixels are copied as they are). Patch 3039 (`ps3_audio_set_host_mix`) lets the port add its sound into every mixed block, on the mixing
thread, for the SDL and the WASAPI backends alike.

- `port/kz1hd/trophies.cpp`: the HLE's notice and the count so far (taken on the unlocking thread, so two unlocks
  read between two frames still count 1 and 2) go through the ring to `kz_trophies_pump`, then `kz_notice_add`.
- `port/kz1hd/trophy_notice.cpp`: reads the trophy's TROPnnn.PNG from the player's TRP (`ps3_trophy_icon`,
  stb_image), queues the card (`trophy_notice.h`: at most three on screen, the rest waiting in order; 250 ms in,
  5 s held, 250 ms out; 470 x 96 at 1080 lines, 56 px from the top and right, 14 px apart, scaled by the window's
  height; when a card leaves, the ones below slide up in 200 ms), draws it once per window size on the CPU (Rajdhani
  Bold and Medium built in from `assets/`, stb_truetype) with premultiplied alpha: the panel at 94 to 90 %, the
  border, icon and words solid, the two cut corners clear. No grade stripe (Gustavo, 2026-11-26): the grade shows in
  the hex chip and the grade word. Words in the game's language (PS3_LANG, as the TRP's names: en, fr, es, de, it, nl).
- The sound (`assets/trophy.mp3`, Project Vekta's own, 0.84 s, 48 kHz stereo; ID3 tags removed, decoded PCM
  md5 `a66cf06c0825070e910381f4cb8c1a29` with ffmpeg) is decoded with minimp3 at start and mixed at half
  amplitude (ASSUMPTION: the port does not read the game's own volume settings) when a card appears.
- PC page: "Trophy Notices" and "Trophy Sound" (`trophy_notices`, `trophy_sound` in `kz1hd_pc.ini`, on by default,
  applied at once); the sound row is greyed out while notices are off (`pc_deps.h`).
- A blocked session (debug options, scripted input) gets no notice: the HLE never notifies a blocked unlock.

Checked: `tools/tests/test_trophy_notice.sh` (queue, timing, stacking, scale, the slide up) and
`test_pc_deps.c`. Game run (2026-11-26, Linux, RTX 3070 Ti, window 1920x1080, V-Sync on, 60 fps): fresh profile,
Level 1 through PAD_SCRIPT, two unlocks forced from the level's Lua by a local diagnostic (not committed; it also
lifted the scripted-input block): one card ("Burn Baby Burn", 1 / 59), and two stacked ("Supply and Demand",
2 / 59) in the present dumps; `[trophy] notice shown ... sound played` in the log, and the sound found in the
`AUDIO_WAV` capture of the mix where the card appeared (normalized correlation 0.98). With `trophy_sound=off` the
card shows without the sound, with `trophy_notices=off` neither.

Again on 2026-11-26 after the blend and the slide (same machine and window, Level 1, two unlocks 3 s apart): the
panel lets the frame through and the corners are clear (present dumps); the second card moved from y 166 to 56 in
about 170 ms, ten presents, once the first left (a local log of the placed rows). The PC page (pause menu, window
1920x1080, input from a file): with Trophy Notices Off, Trophy Sound is greyed and the pad steps over it both ways;
On again, it is selectable.

## The Trophies page (stage 3, 2026-11-27)

A page in the pause menu, made like the PC page (`pcoptions.cpp` injects its Lua into each of the game's Lua
states), text first: the item "Trophies" (the game's language) between PC and Restart opens `gScreenTrophies`.

- Data: `trophies.cpp` reads the HLE's store (`ps3_trophy_count`, `ps3_trophy_get`; no new guest calls) and
  `trophy_list.h` makes the rows in the TRP's order (by id): the grade word and the name on the left, the unlock day
  (the player's local date, `YYYY-MM-DD`) or "Locked" on the right, and the description. A hidden trophy still locked
  shows only the hidden word. The rows reach each Lua state as `KZTRDATA`, in pieces of at most 4 KB (`lua_run` copies
  its text to the guest's stack), once per state and again after an unlock (`kz_trophy_gen`).
- The menu's text is one byte per letter, Latin-1: the TRP's UTF-8 is converted (a letter beyond Latin-1 becomes
  `?`). Its font has no middle dot, so grade and name are three spaces apart.
- Each row is an attribute edit with its value as its only option (`PC settings.TrophyRow`, which always reads 0 and
  takes nothing), so the list has two columns like the other option pages; the selected row shows the edit's arrows,
  which change nothing. The description of the selected row is a text under the list (`GetSelectedItemIndex` in
  the page's `Update`, which also handles Back).
- The count and its bar are the notice's card in the style B palette (no icon, the chip in steel), at the top right
  while the page is open: its `Update` pings `PC settings.TrophyPing` every frame (`kz_trophy_page_ping`, 300 ms
  without a ping closes it); notices arriving meanwhile stack under it. The page's title also says "TROPHIES 2 / 59".
- Pad and keyboard (through the key map) move as in any list; the mouse is the PC page's (`menu_mouse`: on a list
  longer than its box, the pointer's height picks a row as a scroll bar would).
- Not done: icons in the list, the frontend's menus (the page is only in the pause menu).

Checked: `tools/tests/test_trophy_list.sh` (order, count, words in six languages, hidden, Latin-1, the Lua's
escaping and pieces). Game (2026-11-27, Linux, RTX 3070 Ti, window 1920x1080, Level 1, a store with 2 of 59, input
from a file): the page lists the 59 rows and scrolls to row 40 with the pad, the description follows the selection,
Back returns to the pause menu and the header card goes; a mouse click selects the row under the pointer; with
`PS3_LANG=2` the item, title, words, names and descriptions are French with their accents.

## Debug options that block trophies

When a debug option that changes what the player can do is on, the port calls `ps3_trophy_set_blocked(1)` and
an unlock is logged (`blocked by debug option`) and not stored: `KZ_UNLOCK_LEVELS` (every level open in the
level select), `KZ_DEBUG_MENU` (the pause menu's God Mode and Victory Status), `KZ_DEBUG_GOD`, `KZ_DEBUG_GIVE`
(weapons) and `KZ_POKE` (memory writes). Runs driven by a control script block too, since the input is not the
player's: `PAD_SCRIPT`, `PAD_FILE`, `PAD_STICK`, `PAD_STICK_FILE`, `PAD_SWEEP`, `PAD_AUTOPRESS`, `PAD_VIRTUAL_FILE`,
`PAD_MOUSE_FAKE`, `PAD_MOUSE_POS`, `PAD_MOUSE_POS_FILE` (cellPad's test inputs) and `KZ_ROUTE_PLAY` (a recorded
route played back). Add any new such switch to `blocking_option()` in `port/kz1hd/trophies.cpp` and to this list.
