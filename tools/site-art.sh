#!/bin/sh
# Renders the project's own art (assets/*.svg) to the PNGs the README, the site and GitHub use. Needs rsvg-convert
# (librsvg) and the Fira Sans Condensed font (OFL) installed.
set -e
cd "$(dirname "$(readlink -f "$0")")/../assets"
rsvg-convert -w 1280 banner.svg -o banner.png
rsvg-convert -w 2560 banner.svg -o banner@2x.png
rsvg-convert -w 1280 social-preview.svg -o social-preview.png
