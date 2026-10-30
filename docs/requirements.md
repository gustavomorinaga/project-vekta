# Requirements

## Your game

Your own Killzone HD (NPUA80856) from the Killzone Trilogy disc, installed, with its decrypted EBOOT and data
archive, and the decrypted `libsre` and `dev_flash` of PS3 system software 4.93: [GETTING-FILES.md](GETTING-FILES.md).
No PlayStation Network account is used.

## PC

| | Tested | Minimum |
|---|---|---|
| Operating system | CachyOS (Linux x64); the maintainer's Windows install for the cross-built exe; Wine and Proton (GE-Proton 11, proton-cachyos) | Linux x64; Windows x64 (other Windows versions and machines not tried) |
| CPU | the maintainer's desktop CPU with AVX-512 (`KZ_MARCH=x86-64-v4`) | x86-64 with AVX2 (`x86-64-v3`, Intel Haswell or AMD Zen and newer), the default build target; `KZ_MARCH=""` builds for any x86-64, untested |
| GPU | NVIDIA GeForce RTX 3070 Ti, driver 615 | a Vulkan GPU and driver; other vendors not tried yet |
| Memory | | building: several GB for the lifted code (use fewer jobs on small machines); playing: not measured |
| Disk | | about 4 GB for the game files, plus the build (`work/`), a few GB |

How fast it runs: 60 fps steady at the default settings; about 131 to 134 fps in the 144 Hz mode in Level 1 and
Level 7 (Linux, RTX 3070 Ti). The main PPU thread is the limit, so a faster CPU core matters most.
Reports from other hardware are welcome: open an issue with the performance template.
