# The SPURS lock-free queue without libsre (2026-11-17)

Up to 0.5.1 the port needed one file from the player's PS3 system software 4.93 besides `dev_flash`: `libsre.prx`,
decrypted, lifted by `tools/lift.sh` into `work/fw` and loaded at 0x02000000 by `port/kz1hd/stubs.cpp`, for two
imports the runtime did not have. From 0.6.0 the runtime answers them itself (ps3recomp patch 1022,
`libs/spurs/cellSpursLFQueue.c`) and nothing of libsre is read, lifted or linked. `dev_flash` is still needed for the
music: the game's MP3 decoder, `flashMP3.pic`, is a separate piece of the same firmware (below).

## What libsre was used for

Two imports of the EBOOT, nothing else:

| Import | NID | Stub | The game's one caller |
|---|---|---|---|
| `_cellSpursLFQueueInitialize` | `0x011EE38B` | `0x52EB38` | `func_003A84F0` |
| `_cellSpursLFQueuePushBody` | `0x8A85674D` | `0x52EE10` | `func_003A8274` |

`func_003A84F0` builds the queue: `_cellSpursLFQueueInitialize(spurs | 1, q, q + 0x80, 0x20, depth, 3)` on memory it
has just zeroed, then `cellSpursLFQueueAttachLv2EventQueue(q)` (an HLE answer that creates nothing); any error is the
game's own assert. So the queue is ANY2ANY (direction 3), owned by the SPURS instance rather than a taskset, with
32-byte elements right after the 128-byte line. `func_003A8274` pushes a 32-byte request with `isBlocking = 3` and
asserts on any error. The consumer is the Edge zlib inflate task on the SPUs; the producers are PPU threads (the
level streaming and MultiStream's loader for the music). Two queues are built in a session (`0x11194E00`,
`0x111F3A00` in the logs).

## What libsre does there (firmware 4.93, load base 0x02000000)

Read with Ghidra 12.1.4 (a raw import of the relocated image as `PowerPC:BE:64:A2ALT-32addr`, `r2` = 0x02039AB0) and
the disassembly; the facts below are the ones the native code follows.

- **Initialize** (`0x02017134`): an owner aligned to 16 is a taskset, and one whose `+0x74` is above 31 is refused
  (`0x80410902`); otherwise `0x0200209C` checks the arguments (NULL, size at most 0x4000 and a multiple of 16, depth
  1-0x7FFF, direction 0-3, the line 128-aligned and the buffer 16-aligned), asks the SDK version (syscalls 1 and 25;
  above 0x17FFFF it also requires a zeroed line and compares the owner and direction on a second call), and runs the
  init state at `+0x2C` (0, 1 while built, 2 when done) with `lwarx`/`stwcx.`. The bytes (`0x02001F6C`): the counters
  zero, `+0x10` size, `+0x14` depth, `+0x18` buffer, `+0x24` direction, the waiter arrays zero, `+0x70` the owner;
  for ANY2ANY the buffer's bit 0 set, `+0x20`/`+0x21` 0xFF, `+0x28` -1, `+0x30` and `+0x50` 0xFFFF, `+0x78`/`+0x7C`
  zero. A second call on the same queue answers `0x80410902`, because it compares the buffer with the stored one,
  which carries bit 0.
- **PushBody** (`0x020171B8`): alignment and NULL checks, then for direction 3 the ANY2ANY reserve (`0x020030B8`,
  called with `useEventQueue = 1`), the element copy into slot `index mod depth`, and the commit (`0x020035C8`) with
  the queue's `fpSendSignal` (libsre's own `cellSpursSendSignal`); errors come back as `(code & 0xFF) | 0x80410900`.
- **Reserve**: the push word `+0x08` holds pushes committed, the commit mask, the push-waiter pack and pushes
  reserved; the pop word `+0x00` the same for pops. The push is refused for now when the reserved pushes not yet
  popped plus the push waiters already woken reach the depth, when the pushes in flight plus those reach 16, or when
  push waiters are registered and not yet woken (they go first). Then `isBlocking = 0` answers AGAIN (`0x80410101`),
  as does a full waiter ring (15); a blocking push writes 0xFFFF (a PPU waiter) in the push-waiter ids, counts itself
  in, and sleeps on the event queue at `+0x7C`.
- **Commit**: the element's bit goes into the commit mask; the committed index moves over the leading run of set bits;
  for each element that becomes visible, one pop waiter (an SPU consumer, its id `(wid << 8) | task` at
  `+0x32 + 2 * (counter mod 15)`) is woken while the pops in flight plus the woken ones stay at most 15, then one
  push waiter if there is room. Waking a pop waiter changes the pop word, so then the pop word is committed first
  and the push word after it, unchanged since it was read. `0x0200439C` hands the ids to `fpSendSignal`, or, for a
  PPU waiter, to the event port at `+0x28` or `+0x30`/`+0x50`.

The native code does all of that, with one difference on purpose: a blocking push that libsre would put to sleep tries
again until there is room instead. The event queue it would sleep on exists only after
`cellSpursLFQueueAttachLv2EventQueue`, which this runtime answers without creating one, so the libsre path had
nothing to wait on either. In the Level 4 trace below no push found more than one element waiting.

## How it was checked against libsre

A local harness (not in the repository: it needs the player's libsre) built the port with both the lifted libsre and
the native code, and at the first queue initialisation ran random cases through each on two copies of the same
128-byte line in guest memory: random arguments and line contents for Initialize (state 0, 1 or 2, a matching earlier
init or not, misaligned or NULL pointers, a taskset or SPURS owner, sizes and depths out of range), and for PushBody
random but mostly consistent counters, commit masks, waiter packs with up to 15 waiting or woken, waiter ids
including 0xFFFF, event ports, directions 0, 1 and 3, `isBlocking` 0 and 3, and a `fpSendSignal` that fails on a
chosen call. Compared: the result, the 128 bytes of the line, the element buffer and the list of wake-ups.

| Seed | Cases (Initialize + PushBody) | Mismatches | PushBody results |
|---|---|---|---|
| 1 | 20,000 + 20,000 | 0 | 17,158 ok, 7,368 AGAIN, 3,880 errors; 2,455 wake-ups |
| 3 | 20,000 + 20,000 | 0 | 17,035 ok, 7,484 AGAIN, 3,803 errors; 2,503 wake-ups |
| 11 | 3,000 + 3,000 | 0 | 2,604 ok, 1,086 AGAIN, 595 errors; 367 wake-ups |

(A first run of 2,000 also tried direction 2: there libsre runs its PPU2SPU code, which the native code does not
have; those were the only mismatches.) 103 of the seed-11 cases, chosen to cover each kind of result and wake-up, are
`tools/tests/test_lfqueue_vectors.txt`: line before, result, line after and wake-ups, as libsre gave them.
`tools/tests/test_lfqueue.sh` replays them against the native code, and checks the line Killzone HD's own init gets,
a blocking push into a full queue that lands after a pop, and the wake-up of a SPURS-owned queue.

## In the game

Game runs, 2026-11-17, Linux, headless, the same executable with and without the libsre image (so libsre or the
native code answers the two imports):

- Level 1, the rebased `fase1-v040` route (`tools/perf/ab.sh`, 2 pairs): every run replayed the same 15,305 frames
  with the same 12,083 position differences against the recording (at most 103) and all 53,365 events; silent audio
  17.2 % of the 5 s windows in both (`AUDIO_RATE=1`; the level's quiet stretches). The route makes 6 pushes.
- Level 4 (Strange Company), from the menu with every level unlocked, God mode, 3 minutes standing at the start,
  music on (`flashMP3.pic` loaded): 5 pushes in each build, all answered 0; with `PS3_LFQ_TRACE=1` each native push
  woke the waiting Edge zlib task (the pop-waiter pack moved `0x0401`, `0x0822`, `0x0C43`, `0x1064`, `0x14A5`). Silent
  10.0 % of the 5 s windows in both. The frame dumps show the level running.
- Not checked: the Level 7 route (the replay did not start in either build: the menu path from that save, not the
  change), a long session, Windows.

## What still needs the firmware

`dev_flash/sys/external/flashMP3.pic` (4.93): MultiStream (SPU image 3) loads the firmware's MP3 decoder into its
local store at 0x1A880 and calls its entry (0x74A0 in the file) at LS 0x21D20 for every music stream; `tools/lift.sh`
lifts it and `stubs.cpp` registers it. Without it the music and every sound stop at the first MP3. Replacing it means
an MP3 decoder on the host behind that SPU call, with the decoder's own interface in local store; that is a separate
piece of work.
