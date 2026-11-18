# The music decoder without the firmware's flashMP3.pic (2026-11-18)

The game's sound library, MultiStream, decodes its MP3 streams (music, many effects and voices) on an SPU (image 3)
with a decoder it loads from the system software: `/dev_flash/sys/external/flashMP3.pic`. Up to now the port lifted
that file from the player's PS3 system software 4.93 (`tools/lift.sh`) and ran it. `port/kz1hd/native/flashmp3.c`
now answers the call with minimp3 (`third_party/minimp3`, CC0 1.0, commit ea99364f, SHA-256 in its README);
`KZ_MP3_PIC=1` (default off) runs the lifted firmware decoder instead, for comparison. The game's loader still reads
`flashMP3.pic` from `dev_flash` before it uses the decoder, so the file is still needed until it gets a stand-in (the
next step).

## The call

The .pic is an SPU program with its symbols (`decodeFrame` at 0x74A0, `readFrame`, `decode_header`, Layer III
`III_dehuffman`, `III_hybrid`, `synth_filterbank` and their tables: an mpglib-style decoder). MultiStream loads it at
LS 0x1A880 and calls the wrapper at LS 0x21D20 once per frame. Read from the wrapper (0x21D20 to 0x22300) and from
traces of the calls in Levels 1 and 4 (a local hook on 0x21D20 logging registers and local store before and after,
and image 3's DMAs):

| Register | What it is |
|---|---|
| r3 | the frame, from its header; one stream's frames in order (r3 moves by each frame's length) |
| r4 | a block: word 0 = bytes consumed, which matched the length the header gives on every traced call |
| r5 | a block (r4 + 0x10): word 0 = PCM bytes written (0x900 MPEG-1, 0x480 MPEG-2), -1 when the frame gave none |
| r6 | 0x5800 bytes of work space, cleared on entry; the PCM is at r6 + 0x900, big-endian 16-bit, and r3 and r5 return that address |
| r7, r8 | channel 0 and 1 overlap state (0x900 bytes each) |
| r9, r10 | channel 0 and 1 synthesis state (0x880 bytes each) |
| r11 | the bit reservoir: the last 512 bytes of the stream's main data |
| r12 | a block of MultiStream's (its word 0 cycles 1, 0xD, 9, 5); the wrapper puts it back as it found it |

Where the state lives: MultiStream decodes several streams at once (three or more MPEG-2 effect streams at a time
beside the music), call by call through the same LS addresses. Before a stream's call it loads that stream's state
block into LS 0x34100 to 0x35480 (r9, r7 and r11 for a mono stream) from the stream's own address in main memory, and
saves it back after (GET and PUT of 0x1380 bytes, 106 of each in one trace). Between calls it loads other data over
the decoder's own variables (GET LS 0x2A900, 0x380 bytes) and its work space, so the .pic keeps nothing of its own: the
state block is all the state. When a stream starts, the block is zeros.

## The native decoder

`kz_flashmp3_entry` is registered at LS 0x21D20 in image 3 before the lifted .pic, so it takes the call. It keeps
minimp3's state in the same buffers: the overlap in r7/r8, the synthesis entries in r9/r10 (minimp3 keeps only the
even ones for a mono stream), the reservoir in r11, its byte count, the last header and a mark in r7's second half.
No mark means a new stream. Two things follow the firmware decoder rather than minimp3 alone:

- The reservoir is the raw last 511 bytes of main data (zeros at a stream's start), where minimp3 keeps only the bytes
  a frame left unused. The game's encoder points `main_data_begin` into bytes earlier frames already used; minimp3
  alone refused those frames (no PCM) and decoded the frames after them wrong.
- A stream's first frame: the firmware gives -1 for MPEG-1 and a full frame for MPEG-2 (every stream start traced:
  9 MPEG-1 and 174 MPEG-2 in one Level 1 run). `ASSUMPTION:` that is the rule; it was seen, not read out of the
  decoder.

## How it was checked

- A local harness (in a firmware build) ran the lifted .pic for real and the native decoder on a copy of the local
  store on every call, with the native state kept apart per stream (keyed by the address MultiStream loads the state
  block from), and compared the bytes consumed, the PCM bytes and the samples:
  - Level 1, 180 s of play: 42,000 calls, every consumed count and every PCM count the same, 41,991 frames compared,
    SNR 50.7 dB, largest sample difference 857 (of 32,767).
  - Level 4, 180 s at the start: 400 calls, all the same, 391 frames compared, SNR 79.2 dB, largest difference 1.
  - Before the two fixes above: 1,366 of 31,500 calls disagreed on whether a frame gave PCM, and the effect
    streams' samples did not match at all. Decoding the recorded frames offline, the firmware decoder and minimp3 drift
    from a third decoder (FFmpeg) by the same amount frame by frame (within 0.3 dB), and stay closer to each other
    than to it.
- `tools/tests/test_flashmp3.sh`: the call contract at the game's addresses, two streams interleaved through one LS
  with only their state blocks kept (as MultiStream does) decoding as each alone, and the samples against minimp3
  alone (the same). Its two MP3 files were made for the test from a tone and noise.
- Game runs (2026-11-18, Linux, headless, God mode, the same executable, native against `KZ_MP3_PIC=1`): Level 1,
  180 s: silent 10.5 % and 10.3 % of the 5 s windows; Level 4, 180 s: 10.0 % and 10.0 %. No decoder error in any.
  One Level 4 run (with the harness) had no sound from the first window and no decoder call at all; the next was
  normal.
- Not checked: stereo MP3 streams (none was seen in Levels 1 and 4), the other levels, Windows, a long session, a
  listening comparison.
