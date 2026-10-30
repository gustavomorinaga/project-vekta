# Copying or forking this project

For anyone who copies, forks or reuses this repository. The code is MIT-licensed, so you may; this page lists what
to change so your copy does not claim to be this project, and what to keep. Paths are relative to the repository
root. (The shape follows Project Helghast's FORKING.md, MIT.)

## 1. Names and branding

| What | Where | Change it to your own |
|---|---|---|
| Project name "Project Vekta", tag line and repository slug | `README.md`, `site/index.html`, `site/style.css`, `docs/HOW-IT-WORKS.md`, `docs/BUILDING.md`, `.github/workflows/pages.yml` | yes |
| Banner and social preview | `assets/` (`banner.svg` and `social-preview.svg` are the sources; `tools/site-art.sh` renders the PNGs) | yes |
| Site address in the page's metadata | `site/index.html` (`og:url`, `og:image`, `canonical`) | yes |

"Vekta" is the colony world of the first Killzone. Keep the disclaimer in `NOTICE.md` and on the site that your
project is a fan project, not affiliated with or endorsed by the game's publisher or developer.

## 2. Identity and remotes

| What | Where |
|---|---|
| Author and maintainer (name, link, e-mail) | `CITATION.cff`, `.recomp.json`, `.github/CODEOWNERS`, `NOTICE.md` (Reuse and attribution), `LICENSE` (copyright line), README "Official channels" |
| Repository links | `.github/ISSUE_TEMPLATE/*.yml`, `.recomp.json`, `CITATION.cff`, `docs/BUILDING.md` |
| The public e-mail that `tools/check-tree.sh` allows | `tools/check-tree.sh` |
| The author line put on synced patches | `tools/sync-from-private.py` (`PUBLIC_AUTHOR`); you will not need this script unless you also keep a private tree |

## 3. What to keep

- `LICENSE` with the existing copyright line, next to your own: the MIT terms require it for copied code.
- `NOTICE.md`, with every credit in it, and the patch headers' credits. Add your own rows; never remove one.
- The rules in `AGENTS.md`: no game material, no circumvention, ps3recomp untouched, no GPL code.
- `licenses/` for the third-party parts you keep.
