# The project website

How the page at <https://gustavomorinaga.github.io/project-vekta/> is made, checked and published, and the checks a
person still has to do by hand. What it promises its visitors is in [ACCESSIBILITY.md](../ACCESSIBILITY.md#the-project-website)
and [PRIVACY.md](../PRIVACY.md).

## Files

| Path | What it is | Made by |
|---|---|---|
| `site/index.html`, `site/plain.html` | the menu page and the same sections as one plain page with no script | `tools/website.py` |
| `site/404.html`, `site/sitemap.xml`, `site/manifest.webmanifest` | the page for an unknown address, the sitemap (with the screenshots) and the web app manifest | `tools/website.py` |
| `site/style.css`, `site/plain.css`, `site/site.js` | the look, the plain look and the menu, keyboard, game pad and viewer | by hand |
| `site/img/` | the WebP copies of the screenshots and the banner, the PNG icons, `images.json` | `tools/release/site_images.py` (Pillow 12.3.0) |
| `site/fonts/` | Rajdhani Bold (latin) and its licence | by hand, SHA-256 pinned in `check_site.py` |

The sources of the pages are `docs/features.tsv`, `docs/SCREENSHOTS.md`, `docs/requirements.md`, `docs/ROADMAP.md`,
`docs/FAQ.md`, `CHANGELOG.md` and the patch series. After a change to any of them:

    python3 tools/website.py                      # site/*.html, sitemap.xml, manifest
    python3 tools/release/site_images.py          # only after a screenshot, the banner or the icon changed
    python3 tools/release/check_site.py

Never edit the generated files by hand: the docs workflow runs each tool with `--check`.

## What the page must keep

- **Nothing from another host.** No outside script, stylesheet, font or image; the only outside addresses are links
  a visitor follows, and the canonical and feed `<link>`s, which load nothing. Every page carries the same
  Content-Security-Policy (`default-src 'none'`, `base-uri 'none'`, `form-action 'none'`,
  `upgrade-insecure-requests`, Trusted Types with no policy), with `script-src 'self'` on the index only. So
  `site.js` never writes HTML from a string (`innerHTML` and the like throw), never sets a `style` attribute (it sets
  `element.style` properties) and never fetches or stores anything.
- **WCAG 2.2 AA** in both themes, at 320 px and up, with 200 % text and 400 % zoom: headings in order, named table
  headers, focus always visible (and always shown while a game pad is in use), dialogs that hold and return the
  focus, nothing that needs a pointer, the colour pairs `check_site.py` measures.
- **Light.** The first view (page, CSS, script, font, banner) under 250 KB; images with their size; the banner
  fetched first; thumbnails lazy.
- **Every section without script**: CSS shows the section the address names.

## Automated checks

| Check | Where it runs | What it covers |
|---|---|---|
| `tools/website.py --check`, `site_images.py --check` | docs workflow, host tests | the generated files are what their sources give |
| `tools/release/check_site.py [--staged DIR]` | docs workflow, pages workflow, host tests | the files: links, the policy, JSON-LD, headings, tables, contrast, weight, 404, sitemap, manifest |
| `tools/tests/test_website.py`, `test_site.py`, `test_site_images.py` | host tests | each check catches the fault it is for |
| `node tools/site-tests/site_browser.mjs [--only NAME]` | docs workflow (`browser` job) | in Chromium, on the published layout: axe-core in both themes at three sizes, the layout, the viewer, broken addresses, the game pads and keys, the layout shift and bytes, and the published files with the policy in force |

To run the browser tests on your machine (Node 20 or newer):

    cd tools/site-tests && npm ci --ignore-scripts && npx playwright install chromium
    node site_browser.mjs

Lighthouse is not in CI. Against a server that compresses like GitHub Pages it gives 100 for performance,
accessibility and best practices; its SEO score stops at 92 because the policy (rightly) blocks the page from
fetching `robots.txt`, which is how Lighthouse looks for it. Search engines read `robots.txt` themselves.

## Publishing

`pages.yml` runs on a push to `main` that touches the site: `tools/release/stage_site.py _site` puts `site/` at the
root of the address, rewrites the pages' `../assets/` and `../docs/` links, copies only the images the pages name,
and leaves `site/index.html` and `site/plain.html` behind as redirects from the old `/site/` address; then
`check_site.py --staged _site` and the upload. It stays dormant until the repository variable `PAGES_ENABLED` is
`true` and Settings, Pages, Source is "GitHub Actions".

The maintainer's own steps, once, in the GitHub and search engines' settings:

- Settings, Pages: **Enforce HTTPS**.
- Settings, Environments, `github-pages`: deployments from `main` only.
- The repository's About box: the Website field (the address above) and topics.
- Google Search Console and Bing Webmaster Tools: add the address and submit `sitemap.xml`.

## Manual checks

Before a release that changes the site, and after any change to `site.js` or the page structure. Note the browser,
system and date of each pass in the pull request.

1. **Keyboard only** (Firefox and Chrome): Tab from the top reaches "Skip to the content", the menu, every link and
   button of the shown section, the footer; the focus ring is always visible. In the menu: arrows, digits 1 to 0,
   Home, End, Page Up and Down, Enter and the right arrow onto the section's title. Open a screenshot with Enter:
   the arrows change it, Tab stays inside, Esc closes and the focus is back on its thumbnail. The Controls button
   opens the help; Esc closes it.
2. **Screen reader**: NVDA with Firefox on Windows, Orca with Firefox on Linux, VoiceOver with Safari on macOS or
   iOS. The section's title is read when a section opens; the tab title names the section; a table is announced
   with its caption and headers; the viewer says "Screenshot N of M" and the title; the help is read as a dialog.
3. **Game pad** (an Xbox, a PlayStation and a Nintendo or generic pad, wired and Bluetooth): the d-pad and A reach
   every stop of every section, B goes back to the menu, LB and RB change section, LT and RT scroll a page, Start
   opens the help; the footer legend and the help's icons follow the pad last used.
4. **Zoom and text size**: 200 % text (Firefox, View, Zoom text only) and 400 % page zoom at 1280 px: nothing cut
   off, no sideways scrolling except tables and code blocks.
5. **Contrast modes**: Windows High Contrast (Edge or Chrome, forced colours) and the light and dark system themes.
6. **Reduced motion**: with the system's reduce-motion setting on, no fade and no smooth scrolling.
7. **Find in page** (Chrome or Edge): a word from a hidden section is found and its section opens.
8. **Print** (Print preview): every section, light colours, outside links followed by their address, closed lists
   opened.
9. **Without script** (disable JavaScript): the address's section shows; the menu links work; the plain page is
   whole.
10. **Phone** (a real Android and iPhone): the menu, the gallery and the viewer (swipe), the home screen icon.
11. **The published address**, after a deploy: `/project-vekta/` loads, `/project-vekta/site/` and an unknown address
    lead back to it, the response's `Content-Security-Policy` shows no violation in the console, and a link pasted in
    a chat app shows the preview image and title.
