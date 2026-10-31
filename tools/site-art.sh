#!/bin/sh
# Renders the project's own art (assets/*.svg) to the PNGs the README, the site and GitHub use. Needs rsvg-convert
# (librsvg), ImageMagick (the icon) and the Fira Sans Condensed font (OFL) installed.
set -e
cd "$(dirname "$(readlink -f "$0")")/../assets"
rsvg-convert -w 1280 banner.svg -o banner.png
rsvg-convert -w 2560 banner.svg -o banner@2x.png
rsvg-convert -w 1280 social-preview.svg -o social-preview.png
# The Windows launcher's and setup program's icon (installer/art/vekta.ico; ImageMagick's convert joins the sizes).
t=$(mktemp -d)
for s in 256 128 64 48 32 24 16; do rsvg-convert -w $s -h $s icon.svg -o "$t/i$s.png"; done
convert "$t/i256.png" "$t/i128.png" "$t/i64.png" "$t/i48.png" "$t/i32.png" "$t/i24.png" "$t/i16.png" ../installer/art/vekta.ico
rm -r "$t"
