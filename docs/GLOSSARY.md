# Glossary

Terms used in this repository's code and docs.

| Term | Meaning |
|---|---|
| **PPU** | The PS3's PowerPC core, which runs the game's main code (the EBOOT). |
| **SPU** | The Cell's co-processors available to games, each with 256 KB of local store; Killzone HD runs skinning, post-processing (MLAA) and the MP3 decoder on them, among other jobs. |
| **EBOOT** | The game's main executable. The port lifts the decrypted EBOOT the player makes from their own copy. |
| **Lift** | Translating PPU or SPU machine code into C++ ahead of time (`tools/lift.sh`), with ps3recomp's lifters. |
| **ps3recomp** | The static recompiler and PS3 runtime this port is built on (<https://github.com/sp00nznet/ps3recomp>), pinned as `third_party/ps3recomp/` and changed only through `port/patches/`. |
| **HLE** | High-level emulation: a PS3 system library call answered by a PC implementation instead of by the original library's code. |
| **lv2** | The PS3's kernel; its system calls are reimplemented by the runtime. |
| **libsre** | The PS3 firmware's SPU runtime library (SPURS and its job and task models). The game loads it; the player supplies it from their own firmware. |
| **SPURS** | The PS3's SPU scheduler: workloads, tasksets and job chains that the SPUs pick up. |
| **WWS job manager** | The job manager the game uses on top of SPURS: a policy module and job modules that run its SPU work (`jm_post`, `jm_gfx`, ...). |
| **RSX** | The PS3's GPU. The game's RSX command stream is replayed on Vulkan. |
| **GCM** | The PS3 library the game uses to build RSX command buffers and manage video memory. |
| **PSARC** | The PS3 archive format holding the game's data (`data_ps3.psarc`). |
| **VFS** | The folder the port runs from (`work/vfs/`), laid out like the PS3's file system (`tools/setup-vfs.sh`). |
| **Game tick** | One step of the game's logic: 1/30 s on the PS3; `tools/play.sh` runs 60 ticks a second (`KZ_TICK_HZ`, 30 is the original). |
| **ENHANCEMENT:** | A code or doc mark for something the PS3 game does not do; anything that changes gameplay is optional. |
| **ASSUMPTION:** | A mark for a value or behaviour not yet confirmed in the game binary; not a fact. |
