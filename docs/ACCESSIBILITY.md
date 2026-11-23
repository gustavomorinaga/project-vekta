# Accessibility

What the port offers today for controls, prompts, the picture, text and the install, and what it does not. Each item
is in [FEATURES.md](FEATURES.md) with its setting and where it was checked. Settings are on the **PC** page in the
game's Options and Pause menus, kept in `kz1hd_pc.ini`.

## Controls

- **Keyboard and mouse and a controller at the same time.** Controllers go through SDL: Xbox, PlayStation, Nintendo
  and generic pads, with the PS3 layout and rumble.
- **Mouse look** with its own horizontal and vertical sensitivity and invert (`mouse_yaw`, `mouse_pitch`,
  `mouse_invert`), and the mouse works in menus and dialogs.
- **Controller sensitivity and invert** in the game's own Pause > Controls.
- **Remapping keys:** edit `kz1hd_keys.ini` (written on the first run). There is no in-game remapping page yet; it is
  planned.
- `PAD_MOUSE_STICK=1` puts the mouse on the right stick instead of direct mouse look.

## Button prompts

The on-screen prompts and the Controls picture follow the device you last used: keyboard and mouse, Xbox,
PlayStation or Nintendo.

## Picture and motion

- **Motion blur, film grain and glow** from 0 to 100 % in steps of 10 (100 % is the game's own value), and the
  game's anti-aliasing on or off.
- **Field of view** from 70 to 120 degrees at 16:9, for the world and, apart, for the first-person weapon.
- **HUD size and position**, and the **crosshair size** apart from the HUD.
- **Window, borderless or fullscreen** (Alt+Enter switches), any resolution and aspect ratio, with menus and movies
  kept 16:9.
- Not offered: a way to turn off camera shake, and colour-blind filters.

## Text and language

- Voice, subtitles and menus in English, French or Spanish, chosen on the PC page.
- The game's subtitles and fonts are drawn as shipped; their size cannot be changed yet.

## Install and launch

- The release folders and launchers can build with no window or question (`--prepare`), and each failure has its own
  exit code and fix text ([TROUBLESHOOTING.md](TROUBLESHOOTING.md)).
- The project site has a plain page with no script, no web fonts and no animation (`site/plain.html`).

Missing something you need? [Open an issue](https://github.com/gustavomorinaga/project-vekta/issues/new/choose).
