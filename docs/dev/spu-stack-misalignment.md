# SPU host stack misalignment crash (open, 2026-11-11)

One crash, not reproduced since. Kept here so it can be picked up again if it comes back.

## What happened

2026-11-11, Level 11 (`11.SDDockingBay`), game clock at 144 Hz, Linux build: the game closed when a rocket launcher
shot exploded. SIGSEGV with `si_code` SI_KERNEL (a general-protection fault), on a SPURS taskset thread.

The faulting thread ran the lifted SPU image 3 (`spu_0001_at_0056FC80`), function `spu_func_00008154`, at its
prologue: `vmovdqa %xmm0,(%rsp)`. The host stack pointer was 8 bytes off 16-byte alignment at the function's entry,
so the first aligned stack store faulted.

## What the core showed

- Live host chain: `spu_async_thread` -> `spu_run_lifted_job_abi` -> `spu_run_with_halt` (the ordinary entry path,
  not the `longjmp` restart) -> `func_00003050` -> `func_000163F0` -> `func_000078F8` (host_depth 2). From `78F8` the
  guest reaches `8108`/`8154` only through `SPU_DIRECT` tail calls, which keep the stack where `78F8` was entered.
- `8154` was entered 0x1788 bytes (251 x 24) deeper than that. Every 24 bytes in between held the `spu_context`
  pointer and nothing else was rewritten, as if 251 steps had each left 24 bytes on the stack.
- In that run nothing that moves the stack outside ordinary call and return had happened: `steps` was 1 (one drain
  hop), interrupts were never enabled (`int_enable` 0, `event_mask` 0, no `irq_frame`), no stack-reset entries were
  registered, and the halt landing pad was the ordinary one.
- A stack-depth walk over every compiled function of the binary (each `ret` and each tail jump, every path) found
  no unbalanced path, there is no assembly in the build, and no signal handler writes the stack pointer back changed.

So the cause is still open: something moved the stack 24 bytes at a time and static analysis does not show what.

## The stack tripwire (`[stackwatch]`)

A diagnostic patch for `runtime/spu/spu_channels.c` (below). Every `spu_wrch`, `spu_rdch` and `spu_rchcnt` called
from lifted code records the SPU pc, the link register, the channel, `host_depth` and the host stack depth below the
run's start in a per-thread ring of 1024 entries. The first call whose caller's stack is not 16-byte aligned prints
the ring as `[stackwatch]` lines to stderr (`play.log`) and aborts, so a core is written. A 24-byte step flips the
alignment, so the first channel operation after a step trips it, and the ring shows at which SPU pc the depth jumped.

It is not in `port/patches`: it costs a frame pointer and a ring write in the three hottest channel calls. To use it:

1. Copy the patched tree: `rsync -a --exclude .git work/ps3recomp-patched/ work/ps3recomp-diag/`.
2. Apply the diff below inside `work/ps3recomp-diag` (`patch -p1 < stackwatch.diff`).
3. Build beside the normal build:
   `KZ_BUILD=$PWD/work/build-diag PS3RECOMP_DIR=$PWD/work/ps3recomp-diag python3 tools/kzbuild.py build`.
4. Play with it: `KZ_BIN=work/build-diag/KZ1HD tools/play.sh`. If it trips, keep `play.log` and the core
   (`coredumpctl`).

Checked 2026-11-11: a 90-second headless boot with it showed no false trip, and two full plays of the last chapter
by hand at 144 Hz neither tripped nor crashed. The build of the crash lacked commit
`747bed70` (script messages kept on frames without `OnUpdate`); the tripwire build had it. No link to an SPU job is
known, but it is the one other difference.

## Next steps if it comes back

- Read the `[stackwatch]` ring: the entry where `off` grows by 24 names the SPU pc of the step that leaks.
- Lift that function and look at its host code; then a ps3recomp patch in the `1xxx` block with a host test.

```diff
--- a/runtime/spu/spu_channels.c
+++ b/runtime/spu/spu_channels.c
@@ -134,6 +134,7 @@
 }
 
 static SPU_TLS jmp_buf s_spu_halt_env;
+static __thread uintptr_t t_sw_base;   /* DIAGNOSTIC stackwatch */
 static SPU_TLS int     s_spu_halt_armed = 0;
 
 /* Call right after a setjmp returned 0. On 64-bit Windows longjmp unwinds every
@@ -347,6 +348,8 @@
     jmp_buf outer_env; const int outer_armed = s_spu_halt_armed;
     if (outer_armed) memcpy(outer_env, s_spu_halt_env, sizeof(jmp_buf));
     s_spu_halt_armed = 1;
+    const uintptr_t sw_outer_base = t_sw_base;
+    t_sw_base = (uintptr_t)__builtin_frame_address(0);
     g_spu_trampoline_fn = 0;                        /* no stale transfer pending */
     /* Lockstep gate (env SPU_LOCKSTEP, default off): join the round-robin
      * ring and BLOCK until this ctx holds the run token, so only one lifted SPU
@@ -388,6 +391,7 @@
     }
     yz_lockstep_unregister(ctx);   /* leave the ring; hand the token onward */
     s_spu_halt_armed = outer_armed;
+    t_sw_base = sw_outer_base;
     if (outer_armed) memcpy(s_spu_halt_env, outer_env, sizeof(jmp_buf));
     spu_float_mode_leave(fpcr_outer);
     return halted;
@@ -1002,8 +1006,42 @@
 /* ===========================================================================
  * Channel write
  * ===========================================================================*/
+/* DIAGNOSTIC (Vekta crash hunt, not for release): every channel op from lifted
+ * code records (pc, link, host stack depth below the run's base) in a per-thread
+ * ring; the first call from a caller whose stack is not 16-byte aligned dumps the
+ * ring to stderr and aborts so a core is written. */
+#include <signal.h>
+typedef struct { uint32_t pc, lr, ch, depth; uint64_t off; } spu_sw_ent;
+#define SPU_SW_N 1024
+static __thread spu_sw_ent t_sw[SPU_SW_N];
+static __thread uint64_t t_sw_n;
+__attribute__((noinline)) static void spu_sw_trip(spu_context* ctx, uintptr_t fp, uint32_t ch)
+{
+    fprintf(stderr, "[stackwatch] MISALIGNED caller: img=%d pc=0x%05X lr=0x%05X ch=%u depth=%u fp=%p base=%p off=%lld steps=%llu\n",
+            ctx->image_id, (unsigned)(ctx->pc & SPU_LS_MASK), ctx->gpr[0]._u32[0] & SPU_LS_MASK, ch,
+            ctx->host_depth, (void*)fp, (void*)t_sw_base, (long long)(t_sw_base - fp),
+            (unsigned long long)ctx->steps);
+    uint64_t n = t_sw_n, k = n < SPU_SW_N ? n : SPU_SW_N;
+    for (uint64_t i = n - k; i < n; i++) {
+        const spu_sw_ent* e = &t_sw[i % SPU_SW_N];
+        fprintf(stderr, "[stackwatch] #%llu pc=0x%05X lr=0x%05X ch=%u depth=%u off=%llu\n",
+                (unsigned long long)i, e->pc, e->lr, e->ch, e->depth, (unsigned long long)e->off);
+    }
+    fflush(stderr);
+    abort();
+}
+static inline __attribute__((always_inline)) void spu_sw_note(spu_context* ctx, uintptr_t fp, uint32_t ch)
+{
+    spu_sw_ent* e = &t_sw[t_sw_n++ % SPU_SW_N];
+    e->pc = (uint32_t)ctx->pc & SPU_LS_MASK; e->lr = ctx->gpr[0]._u32[0] & SPU_LS_MASK;
+    e->ch = ch; e->depth = ctx->host_depth; e->off = t_sw_base ? (uint64_t)(t_sw_base - fp) : 0;
+    if (fp & 15) spu_sw_trip(ctx, fp, ch);
+}
+#define SPU_SW(ctx, ch) spu_sw_note((ctx), (uintptr_t)__builtin_frame_address(0), (ch))
+
 void spu_wrch(spu_context* ctx, uint32_t channel, u128 value)
 {
+    SPU_SW(ctx, channel);
     /* SPU_CHHIST=1: which channels a job actually touches, and how often. A
      * worker that never issues an MFC command is either not being handed its
      * work descriptor or is waiting on a channel we never satisfy; the channel
@@ -1275,6 +1313,7 @@
  * ===========================================================================*/
 u128 spu_rdch(spu_context* ctx, uint32_t channel)
 {
+    SPU_SW(ctx, channel);
     spu_honour_stop_request(ctx);
     { static int s_c = -1; if (s_c < 0) { const char* e = getenv("SPU_CHHIST"); s_c = e ? (atoi(e) > 0 ? atoi(e) : 2000) : 0; }
       if (s_c) { static unsigned long long r[128]; static unsigned long long n;
@@ -1372,6 +1411,7 @@
  * ===========================================================================*/
 uint32_t spu_rchcnt(spu_context* ctx, uint32_t channel)
 {
+    SPU_SW(ctx, channel);
     spu_honour_stop_request(ctx);
     /* SPU_WHOPOLLS=<n>: rchcnt/park accounting PER SPU, printed every n calls
      * with the pc each SPU is sitting at.
```
