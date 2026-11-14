# Frame map

What Killzone HD draws in a gameplay frame, pass by pass, as the port's draw engine receives it from the game's RSX
command stream. Every row comes from an F12 capture (`passes.txt`, `draws.txt`; [testing.md](testing.md)) of the
level named, taken on the date given, with the build of the `feat/renderdoc-validation` branch (ps3recomp patches up
to 2067). Under RenderDoc each guest pass is a debug region named as in the "Region" column (patch 2065), and the
port's own GPU passes have regions of their own.

Formats are the guest's: `fmt 3` is A8R8G8B8 (drawn as RGBA8), the zeta is Z24S8 (`zfmt 2`). Texture formats in
`draws.txt` are RSX texture format bytes (`f86` DXT1, `f88` DXT4/5, `f85` A8R8G8B8 swizzled, `fA5` A8R8G8B8 linear,
`fA1` B8 linear); "pipe" is the engine's pipeline number in that run (the same guest program pair has the same number only
within one run).

## Level 1 (2026-11-13)

Route `fase1` replayed at 144 Hz, capture 40 s into the replay (frame 25985 of the run), 16 passes = 4 frames. Every
frame has the same four passes; the two 1280x720 colour targets and their zetas alternate frame by frame (double
buffering: `0x0E640000` with zeta `0x0DEC0000`, `0x0E280000` with zeta `0x0DB00000`).

| # | Region | Target, size, format | Draws | What it is |
|---|---|---|---|---|
| 0 | `pass 0: A 0x0E640000 1280x720 RGBA8 \| Z 0x0DEC0000 1280x720` | the display buffer, 1280x720 A8R8G8B8, zeta Z24S8 | 411-430 | the world: clears (colour, then depth and stencil), about 10 draws with depth writes off first (the backdrop), then the world and characters (most with 1 to 5 DXT textures), alpha-blended and additive effects, then a depth-only clear (`mask 0x01`) and the first-person weapon (about 20 draws, 1 to 4 textures), last one full-screen draw: the colour grading (below) |
| 1 | `pass 1: A 0x000F1000 640x360 RGBA8` | IO `0xF1000`, 640x360 A8R8G8B8 in main memory | 1 | `jm_post`'s motion field: one full-screen quad with no texture, the camera's motion as colour (127,127 = still) ([wws-jm-post.md](wws-jm-post.md)) |
| 2 | `pass 2: A 0x00010000 640x360 RGBA8` | IO `0x10000`, 640x360 A8R8G8B8 in main memory | 1 | the reduced scene and depth for `jm_post`: samples a 1280x720 colour surface and the frame's zeta read as A8R8G8B8 |
| - | `jm_post MLAA (port, GPU) 1280x720`, `jm_post motion blur (port, GPU), field 640x360` | the display buffer, in place | compute | the game's SPU post chain as the port runs it on the GPU (patches 2036, 2043; 2048 under a temporal upscaler); with a temporal upscaler on, `temporal upscaler: DLSS` or `FSR 3.1` instead of the MLAA. Before patch 2080 the GPU MLAA ran only on a 1280x720 image: in a RenderDoc capture at a 3440x1320 internal size these regions were absent, and nothing else antialiased the frame (the glow and the motion blur were skipped too). Since 2080 the region is named with the drawn size (`jm_post MLAA (port, GPU) 3440x1440`), followed by `jm_post glow (port, GPU), over the scaled frame` and the motion blur's |
| 3 | `pass 3: A 0x0E640000 1280x720 RGBA8 \| Z 0x0DEC0000 1280x720` | the display buffer again | 45 | composition and HUD: the first draw blends the previous frame's buffer (`0x0E280000`) over this one; the rest are the HUD's textured quads (DXT 128x128, 64x64, a 512x256 page), depth test on, no depth writes |
| - | `present: display image to the window` (and `FSR 1 (EASU + RCAS)` when chosen) | the window's swap chain | blit | the port's present |

**No HDR scene.** The scene is drawn into A8R8G8B8 targets only. The world pass's last draw samples its own target
(`t0 = 0x0E640000 fA5 1280x720`) and a 256x1 texture (`t1 f85 256x1`, the grading ramp) and writes the graded
picture back to the same target, so the weapon is graded with the world. Nothing in the frame is wider than 8 bits
per channel; an HDR output has to expand the final picture (ROADMAP, Phase 8).

**No shadow maps.** No pass draws into a depth-only or extra target: besides the display buffers there are only the
two 640x360 `jm_post` targets. No draw of the world pass has its colour writes off (every draw's colour mask is
`01010101`), so there are no stencil-volume passes either. One draw (draw 5 of the world pass) multiplies a 128x64
texture into the frame (blend `DST_COLOR, ZERO`): a projected darkening texture, which is how the strings
`ProjectiveShadow` and `Cast Dynamic Shadows` in the EBOOT would show. See "Shadows" below for the other levels and
the projection at `0xDF734`.

## Levels 4, 7, 8 and 11 (2026-11-13)

Each level started from the level list (`KZ_UNLOCK_LEVELS=1`, Normal), the player standing at the start, one capture
100 s after the menu keys (`KZ_DEBUG_GOD=1`, 60 Hz clock), 16 passes = 4 frames. Every level has Level 1's four passes
on the same targets: the world pass on the double-buffered 1280x720 display buffers and their zetas, the two 640x360
`jm_post` passes at IO `0xF1000` and `0x10000`, then 43 to 46 composition and HUD draws on the display buffer. What
changes from level to level is only what the world pass holds:

| Level | World pass draws | Weapon (after the depth clear at draw) | View camera far plane (site `0x21A190`) | Sky camera (`0x2B81DC`) |
|---|---|---|---|---|
| 1 | 411-430 | 389 (about 20 draws) | not logged in that run | not logged in that run |
| 4 | 130 | 108 | 45 | not seen in the run |
| 7 | 324 | 301-302 | 45 | near 1, far 2000 |
| 8 | 461 | 444 | 120 | near 1, far 2000 |
| 11 | 95 | 73 | 80 | near 50, far 2000 |

The world pass's last draw is the grading in every level (the same full-screen draw of its own target through a 256x1
ramp), so the frame's format and the grading are the same everywhere.

## Shadows

**No dynamic shadow maps in any of the five levels.** In none of the captures (Level 1 on a route with combat, Levels
4, 7, 8, 11 at their start) is there a pass into a target other than the display buffers and the two 640x360 `jm_post`
targets, a depth-only pass, or a draw with its colour writes off. What the game does have:

- Lighting baked into textures: the world's draws sample 2 to 5 textures, and the EBOOT has the shader parameter
  names `LightMapColor` and `LightMapIntensity`.
- Textured darkening drawn into the frame with blend `DST_COLOR, ZERO` (multiply): one draw a frame in Level 1 (a 128x64
  DXT5 texture, early in the world pass) and in Level 8 (512x512 DXT5, among the draws after the depth clear
  that starts the first-person weapon); none in Levels 4, 7 and 11 at the points captured. That is a projected texture, not a shadow map: there is
  no pass that renders depth from a light. The EBOOT's strings `ProjectiveShadow`, `Cast Dynamic Shadows`,
  `Receives Dynamic Shadows` and `StencilShadow` name the kinds the engine knows; no stencil-shadow pass (colour off,
  stencil op set) was seen.

So ROADMAP item "Shadows: resolution and distance" has no shadow-map resolution to raise in these levels; the
projected textures could only be drawn at a higher texture resolution if the game had larger ones.

### The projection at `0xDF734`

`0xDF734` is the return address of a virtual call inside `func_000DF638` (call site at `0xDF730`), which builds a
camera's projection through `func_001A9FC8` (`port/kz1hd/fov.h`). `KZ_CAM_LOG=1` in Levels 4, 7, 8 and 11 shows it is
called for two cameras every time, each with exactly the parameters that camera's own site gives it:

| Camera (object) | Also built at | Near | Far | Vertical FOV | Aspect |
|---|---|---|---|---|---|
| first-person camera (`0x12FA1F20` in every level) | `0x24DE04` | 0.01 | 30 | 48 | 16:9 |
| world camera (a level's own object) | `0x1EE3D0`, `0x1B53E8` | 0.1 | 4096 | 56.25 | 16:9 |

So it applies a camera's projection generically (for culling, it seems: nothing is drawn with it alone); it does not
make a light's projection, and nothing in the frame is drawn from a light's point of view. One more site,
`0x1CEC28`, builds a 90-degree 4:3 projection (near 0.1, far 4096) on the world camera in every level; no target or
pass drawn with it appears in the captures (a culling frustum, probably: ASSUMPTION, not traced).

## Stencil bytes read as colour

Pass 2 reads the zeta as A8R8G8B8 through the port's depth-pack shader (`rsx_vk_depth_pack.comp`). Until the
`jm_post` thread's patch 2076 lands, the stencil byte of that read is 0; after it, the real stencil value. A capture
of pass 2's input taken on either side of 2076 differs in that byte.
