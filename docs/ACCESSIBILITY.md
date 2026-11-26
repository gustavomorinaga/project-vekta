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

## The project website

The site aims at [WCAG 2.2](https://www.w3.org/TR/WCAG22/) level AA, in the light and the dark theme. Checked on
every change by axe-core in Chromium at a desktop, a short desktop and a phone size, and by the checks of the page
files (headings, tables, colour contrast); checked by hand with a keyboard, screen readers and game pads before a
release ([dev/website.md](dev/website.md#manual-checks)).

- **Keyboard:** everything works without a mouse. The menu takes the arrows, the digits 1 to 0, Home, End and the
  page keys; Enter opens a section and moves to its title; the focus is always visible. The **Controls** button in
  the footer lists every key and button.
- **Game pad:** Xbox, PlayStation, Nintendo and generic pads move through the menu and every link and screenshot in
  reading order, with the pad's own button pictures in the footer and the Controls list.
- **Screen readers:** the section's title is read when it opens and the tab title names the section; tables have
  captions and named headers; the screenshot viewer says which screenshot of how many is shown.
- **Your settings are followed:** light or dark theme, reduced motion (no fades), high-contrast modes, text zoom to
  200 % and page zoom to 400 %.
- **Find in page** (Chrome and Edge) reaches the text of sections that are not shown and opens them.
- **Print** gives every section, in light colours, with the address after each outside link.
- **The plain page** (`plain.html`) has every section on one page with no script, no web font and no animation.

Missing something you need? [Open an issue](https://github.com/gustavomorinaga/project-vekta/issues/new/choose).
