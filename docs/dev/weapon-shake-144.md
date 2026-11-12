# The first-person weapon shaking above 30 ticks a second (2026-11-12)

Reported by the maintainer on 2026-11-12: in the dropship yard of chapter 9 ("Hidden Pasts"), at 144 fps, the
first-person weapon shakes strongly while walking; the character model looks right.

**State: one cause found and fixed, the yard's is still open.** The fix below (`move_vel.h`,
`native_ppu/move_velocity.cpp`, `KZ_MOVE_VEL30=0` turns it off) removes the shaking in the case reproduced without the
yard. With it the maintainer still sees the shaking in the yard (2026-11-12): that one has another trigger, not found
yet.

## The cause that was found

The weapon's pose comes from an animation tree with a 1-D blend (`func_00196DB8`) whose parameter is anim variable 16
(`func_00197A90` reads it). The first-person character's update (`func_00229ED0`) sets that variable at `0x22A294`
(`func_001BAA98`): 100 times its `+0x424`, which follows the movement record of the state machine at its `+0x164`
(vtable `+0x5C`; the record is `+0x1C` of that object). With the record's flag byte clear it was 0.95, with it set
0.35, and the blend then took another pair of clips (parameter 95 against 35): the weapon's root moved by about 0.1
between frames.

The flag is set by the player controller's tick, `func_00294178`, at `+0x24` of the controller:

    d = dot(player +0x30..+0x38, player +0x80..+0x88)      an axis of the player's, its velocity
    |d| > sin(pi/4)  (pi/4 at 0x294164, func_003C2518 with r4 = 1)   ->  d < 0 ? +0x66 : +0x24

ASSUMPTION: the axis (the second row of the player's matrix) is the direction the player faces, and the two bytes
mean moving forward and moving backward. In the logged run it was (0.255, 0.967, 0), horizontal with z up, the way the
player had walked before a rock face turned him aside; the byte was set from d = 0.723 up and clear up to 0.702.

The velocity at `+0x80` is the position's change over the last tick divided by the tick (in game, equal to the
logged positions' difference times 144). The player in the logged run was pushing into a rock face and sliding along
it, and the collision moved him by 2 to 4 mm from tick to tick; over the PS3's 1/30 s tick that is about 0.1 of
velocity, over a 1/144 s tick about 0.5, and `d` went 0.70, 0.86, 0.77, 0.52 in consecutive frames: across 0.7071
every few frames. In 8 s: 16 crossings with the last tick's velocity, 4 with the velocity over the last 1/30 s.

The fix gives `func_00294178`, while it runs, the velocity over the last 1/30 s of game time (the positions of the
last ticks; interpolated between kept ticks). At 30 ticks a second nothing is replaced.

## How it was checked

- Reproduced without the yard: the maintainer's save of 2026-10-27, Campaign > Continue, then walking forward into
  the rock face by the checkpoint (headless, `PAD_STICK_FILE`). A per-frame log of the weapon's root bone (the
  translation-only matrix at the start of its palette) counted the frames where the root leaves its path for one
  frame and comes back.
- 18 s at 144 ticks a second, alternating runs: 0 and 0 such frames with the fix, 8 and 11 with `KZ_MOVE_VEL30=0`.
  At 60 ticks a second (before the fix) 0. `KZ_JM_SKIP_US=10` did not change it (11 against 11): not the SPU timing of
  the October ghosting ([ps3recomp-v018-migration.md](ps3recomp-v018-migration.md)).
- `tools/tests/test_move_vel.c`: the same test on a simulated slide with +-1.5 mm of jitter changes its flag 0 times at
  30 ticks a second, 1473 times at 144 with the last tick's velocity, 0 with the fix.
- F12 captures are not a good measure here: the frames being captured run slower, take more ticks and so move the
  weapon further.

## Still open

- The yard: with the fix build the maintainer still sees the shaking there (2026-11-12). Not reproduced without the
  yard yet; the per-frame data has to come from a play there.
- Other readers of the player's velocity at `+0x80` keep the last tick's value.
