# Test data

`flashmp3_m1_32k_mono.l3` and `flashmp3_m2_22k_mono.l3` are MP3 streams (MPEG-1 Layer III at 32 kHz and MPEG-2
Layer III at 22.05 kHz, mono) made for `tools/tests/test_flashmp3.sh` from a sine tone and noise; nothing in them comes
from the game. They are not named `.mp3` because the repository ignores that extension to keep game audio out. Made
with FFmpeg n9.0.2 and libmp3lame:

```sh
ffmpeg -f lavfi -i "sine=frequency=440:sample_rate=32000:duration=1.5" \
  -f lavfi -i "anoisesrc=color=pink:sample_rate=32000:amplitude=0.3:duration=1.5:seed=7" \
  -filter_complex "[0][1]amix=inputs=2:normalize=0,volume=0.8" -ac 1 -ar 32000 -c:a libmp3lame -q:a 5 \
  -write_xing 0 -id3v2_version 0 -f mp3 flashmp3_m1_32k_mono.l3
ffmpeg -f lavfi -i "sine=frequency=330:sample_rate=22050:duration=1.5" \
  -f lavfi -i "anoisesrc=color=white:sample_rate=22050:amplitude=0.2:duration=1.5:seed=3" \
  -filter_complex "[0][1]amix=inputs=2:normalize=0" -ac 1 -ar 22050 -c:a libmp3lame -q:a 6 \
  -write_xing 0 -id3v2_version 0 -f mp3 flashmp3_m2_22k_mono.l3
```
