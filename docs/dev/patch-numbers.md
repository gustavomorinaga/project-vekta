# Patch numbers

This repository's patches to ps3recomp are numbered by area (`port/patches/`) and apply in the order of
`port/patches/series`. Before publication they were one series numbered 0001 to 0093 on ps3recomp v0.18.0; the dev
notes written before 2026-10-30 use those numbers, or, before the move to v0.18.0 (2026-10-27/28), the numbers of an
older series on v0.14.0, which do not appear here. This table maps the v0.18.0 series to the public numbers.

| Private series (2026-10-29) | Public number | Patch |
|---|---|---|
| 0001 | 1001 | cellSpurs: look tasksets up by (spurs, wid) for |
| 0002 | 2001 | cellGcm: strict flips (the drain always stops at a |
| 0003 | 2002 | rsx: a linear texture's pitch register applies also |
| 0004 | 2003 | vulkan: PS3RECOMP_VK_DUMP may number its frames (printf |
| 0005 | 2004 | cellGcm: a JUMP-to-self park gets a 250 ms grace before |
| 0006 | 4001 | cellKb: cellKbRead (NID 0xFF0A21B7) is the SDK name of |
| 0007 | 2005 | cellGcm: optional dedicated ISR thread for the |
| 0008 | 4002 | cellPad: keyboard fallback off Windows through SDL's |
| 0009 | 2006 | cellGcm: main-memory reports at IO 0x0E000000; |
| 0010 | 2007 | rsx engine: a draw sampling its own colour target reads |
| 0011 | 3001 | cellSaveData: a callback error is ERROR_CBRESULT, |
| 0012 | 4003 | cellPad: sticks start centred; PAD_TRACE |
| 0013 | 2008 | cellGcm: reject non-IO-mapped EAs on the page table |
| 0014 | 1002 | spu: SSE absdb/avgb; sync is a compiler barrier on x86 |
| 0015 | 1003 | spu: the NEON integer helpers on x86 through a |
| 0016 | 1004 | spu: lighter per-step dispatch, inline MFC parameter |
| 0017 | 1005 | spu_lifter: promote leaf functions' registers to |
| 0018 | 1006 | spu: one-load SSSE3 local-store access |
| 0019 | 1007 | spurs: g_spurs_cur_wid for the sampling profiler |
| 0020 | 1008 | spu_lifter: SPU_LIFT_NATIVE emits named functions as |
| 0021 | 1009 | spu: SPU_DMA_STATS transfer counts per region |
| 0022 | 1010 | spu: DMA fast path without the env-gated diagnostics; |
| 0023 | 3002 | lv2: PS3_USLEEP_MAX_US caps guest sleeps (diagnostic) |
| 0024 | 3003 | lv2: PS3_SYSCALL_HIST per-syscall call counts |
| 0025 | 2009 | rsx: GPU copy for a target sampled while drawn into; |
| 0026 | 3004 | trace: PS3_TRACE_MIN_US drops short spans so long |
| 0027 | 2010 | vulkan: SPIR-V and pipeline caches on disk |
| 0028 | 2011 | gcm: pump the window's input events during long FIFO |
| 0029 | 2012 | gcm: at most PS3_GCM_MAX_QUEUED_FLIPS (2) flips ahead |
| 0030 | 3005 | sysprx: lwcond signals wake waiters |
| 0031 | 2013 | vulkan: pipeline cache saved on its own thread |
| 0032 | 3006 | timer: PS3_TIMEBASE_SCALE runs the guest clock faster, |
| 0033 | 3007 | audio: mixer paced by the device queue on Linux |
| 0034 | 4004 | pad: PAD_KEYLOG times keys against title reads; log |
| 0035 | 3008 | ppu lifter: FP helpers test the result for NaN first |
| 0036 | 1011 | spurs: taskset bitset updates from the host are atomic |
| 0037 | 1012 | spu: hooks to observe SPU transfers and job-module |
| 0038 | 2014 | video: faster texture mutation hash, reused staging for |
| 0039 | 3009 | ppu lifter: --setjmp/--longjmp lift a title's own |
| 0040 | 4005 | cellPad: mouse on the right stick in the SDL keyboard |
| 0041 | 4006 | cellPad: mouse smoothing halves every poll (less lag), |
| 0042 | 4007 | cellPad: raw mouse counts for a title hook |
| 0043 | 4008 | cellPad: free pointer for menus, button injection, |
| 0044 | 4009 | cellPad: key map from a file for the keyboard and mouse |
| 0045 | 3010 | Quieter logs in play; AUDIO_RATE/AUDIO_WAV on Linux; |
| 0046 | 3011 | PPU: between PPU threads a reservation covers the |
| 0047 | 3012 | cellAudio: AddData, Add2chData and Add6chData |
| 0048 | 4010 | cellPad: controllers hot-plug, keyboard together with a |
| 0049 | 2015 | Vulkan window: display mode, size and V-Sync while |
| 0050 | 3013 | PPU: guest stores take a short path when no watch is |
| 0051 | 1013 | SPU coherence: pinned lines (spu_coh_pin) |
| 0052 | 3014 | PPU: leaner hot paths for guest stores, indirect calls |
| 0053 | 3015 | tools: ppu_regcache.py, guest registers in host locals |
| 0054 | 2016 | RSX engine: alpha test enable and function as the |
| 0055 | 2017 | GCM: feed the draw engine only the subchannels that |
| 0056 | 3016 | PPU: AltiVec helpers in host SIMD; native replacements |
| 0057 | 3017 | PPU: vm_block_plain, for native code that stores a |
| 0058 | 3018 | PPU: one window compare on the guest store path |
| 0059 | 3019 | PPU: one window compare on the inline guest load path |
| 0060 | 3020 | PPU: the inline stack stores test one window too |
| 0061 | 2018 | RSX: vertex fetch attribute by attribute; index dedupe |
| 0062 | 3021 | Trace spans for the flip-queue wait and lwcond waits |
| 0063 | 3022 | cellSaveData: cellSaveData_quiesce, for a host that |
| 0064 | 3023 | PPU: watches a host probe can move while the title runs |
| 0065 | 2019 | Vulkan: the frame at the window's size, anamorphic when |
| 0066 | 2020 | Vulkan: crop a stretched frame to its middle; fill only |
| 0067 | 2021 | RSX: F12 captures the next frames' passes |
| 0068 | 2022 | RSX: FACING input in fragment programs; F12 capture |
| 0069 | 2023 | RSX: an F12 capture also writes each texture and its |
| 0070 | 2024 | RSX: an F12 capture also writes each draw's fetched |
| 0071 | 2025 | cellGcmSys: GCM_RING_RECYCLE=0, and recycle a title's |
| 0072 | 2026 | RSX: cheaper per-draw hashing and vertex fetch |
| 0073 | 0001 | Killzone port on v0.18.0: our Vulkan backend on the new |
| 0074 | 1014 | spurs: poll period and idle cap knobs, |
| 0075 | 0002 | Killzone port on v0.18.0: coherence marks dropped, |
| 0076 | 3024 | ppu_lifter: memory-model accessors always inline; stack |
| 0077 | 3025 | ppu_lifter: PPU_STORE_SNAP takes the store diagnostics |
| 0078 | 1015 | spurs: the coordinator sleeps on a condition variable |
| 0079 | 3026 | ppu_lifter: PPU_STORE_SNAP computes the memory-model |
| 0080 | 1016 | spurs: ready-count stores take only the lock-line lock |
| 0081 | 2027 | Vulkan: invariant position in the guest vertex programs |
| 0082 | 2028 | Test drivers: frame dumps in bursts, a part of the |
| 0083 | 2029 | Frame dumps on a trigger file: none outside it |
| 0084 | 2030 | Test drivers: the F12 capture on a trigger file; |
| 0085 | 0003 | MinGW (clang): SEH-free sampler stack read, |
| 0086 | 2031 | Vulkan backend on Windows: vulkan-1.dll through |
| 0087 | 1017 | SPURS: cellSpursRequestIdleSpu writes the idle-SPU |
| 0088 | 1018 | SPURS: PS3_SPURS_PM_SELECT=1 answers a policy module's |
| 0089 | 2032 | Vulkan engine: anisotropic filtering as the title asks |
| 0090 | 2033 | Vulkan present: AMD FSR 1 (EASU + RCAS) instead of the |
| 0091 | 3027 | Audio: 5.1 and 7.1 output, |
| 0092 | 5001 | sceNpTrophy: the title's own trophy set (TROPHY.TRP), |
| 0093 | 2034 | Draw engine: main-memory colour targets coherent with |
| 0094 | 4011 | cellPad: key capture for a title's remapping screen; key map reload |
| 0095 | 2035 | NV0039 memory-to-memory copies on subchannel 1 (PS3RECOMP_NV0039) |
| 0096 | 2036 | Killzone HD's MLAA on the GPU (PS3RECOMP_MLAA_GPU) |
| 0097 | 2037 | MLAA pass: no frame waits for the driver to compile it |
| 0098 | 2038 | Vulkan: NVIDIA Reflex through VK_NV_low_latency2 |
| (none) | 0004 | tests: upstream's host tests follow the port's patches |
| (none) | 2039 | cellGcm: the SPU PUT reject reads its own page map, which unmaps never clear |
| (none) | 1019 | spu: the DMA fast path leaves the POISON-DMA report to the full path |

The last three were written in this tree (2026-10-30), from ps3recomp's own tests run on the patched tree
(`tools/tests/test_upstream_suite.sh`); they have no private number until the private branch takes them.
