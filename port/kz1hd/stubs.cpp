/*
 * Killzone 1 HD -- game-specific hooks and overrides (main.cpp is the generic runner).
 *
 * The SPURS lock-free queue (_cellSpursLFQueueInitialize/_cellSpursLFQueuePushBody), which once needed the
 * firmware's own libsre here, is in the runtime: ps3recomp patch 1022, libs/spurs/cellSpursLFQueue.c.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ppu_recomp.h"
#include "spu_context.h"

extern "C" {
void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));
}
static void level_unlocked(ppu_context* ctx) { ctx->gpr[3] = 1; }
static void not_submission(ppu_context* ctx) { ctx->gpr[3] = 0; }

extern "C" {
extern volatile int g_spu_float_mode;   /* spu_channels.c */
void     kz_sail_register(void);   /* sail.cpp: movies through FFmpeg */
void     cellGcmHleSetStrictFlip(int on);
void     cellGcmHleSetIsrThread(int on);
}

void kz_pc_options_register(void);   /* pcoptions.cpp: mouse look, PC options */
#include "route.h"
extern "C" void spu_begin_image(int image_id);
extern "C" void kzmp3_spu_recomp_register(void) __attribute__((weak));   /* <SPU_DIR>/flashmp3, lift.sh */
extern "C" void spu_register_function(uint32_t addr, void (*fn)(spu_context*));
extern "C" void kz_flashmp3_entry(spu_context* ctx);   /* native/flashmp3.c */

extern "C" void ps3_load_prx_modules(void)
{
    kz_sail_register();
    /* KZ1 HD paces on the RSX holding each flip to vblank (KZ_STRICT_FLIP=0 to compare) */
    cellGcmHleSetStrictFlip(!getenv("KZ_STRICT_FLIP") || atoi(getenv("KZ_STRICT_FLIP")));
    /* Its vblank handler waits in the same call its main thread may be waiting in,
     * so it has to run on a thread of its own, as on the PS3. */
    cellGcmHleSetIsrThread(1);
#ifndef KZ_SPU_EXACT_FLOAT
    g_spu_float_mode = 1;   /* round toward zero + FTZ around SPU runs: see CMakeLists, SPU_FAST_FLOAT */
#endif
    /* Unless KZ_JM_SKIP=0, jm_post's jobs never run on the SPUs at all
     * (native/jm.c takes and finishes them): in this port they only ever read
     * empty buffers, and an SPU job reading the depth target stored it to
     * guest memory every frame. ps3recomp v0.18.0's scheduler (#291) runs the
     * other workloads on the instances' own SPUs; its poll period is left at
     * its default (the 50 us of the old kernel thread halved the frame rate). */
    if (!getenv("KZ_JM_SKIP") || strcmp(getenv("KZ_JM_SKIP"), "0"))
        setenv("PS3_SPURS_WID_LANES", "1:0", 0);
    /* The job managers' selectWorkload poll answered with the current workload,
     * so each runs its jobs back to back. Answered 0, jm_gfx (wid 2) went back
     * to the kernel after every job and in busy 144 Hz scenes built the
     * first-person weapon from last frame's bones (docs/dev/ps3recomp-v018-migration.md). */
    setenv("PS3_SPURS_PM_SELECT", "1", 0);
    /* PS3_SPURS_WANTED=2 (default here; PS3_SPURS_WANTED=0 restores the probes):
     * the SPURS kernel admits only workloads the game kicked or that asked for
     * idle SPUs, with no idle probes (ps3recomp patch 1017, after Project
     * Helghast's 1074). Measured 2026-11-10 (rebased routes, 6 pairs each, the
     * same executable): whole-process CPU per frame -4.6 % [-5.0, -4.2] in
     * Level 7 and -6.0 % [-8.5, -3.5] in Level 1, frame rate +0.85 % [+0.19,
     * +1.5] in Level 7; standing still, the SPURS threads 19 -> 15 % and
     * 34 -> 27 % of a core at 60 Hz, 45 -> 37 % and 71 -> 59 % at 144 Hz;
     * replays identical, presents unchanged. */
    setenv("PS3_SPURS_WANTED", "2", 0);
    /* KZ_UNLOCK_LEVELS=1 (testing): every level selectable. The chapter
     * select asks IsLevelUnlocked (func_00298578) through its script binding,
     * an indirect call, which this replaces. */
    if (getenv("KZ_UNLOCK_LEVELS")) ppu_register_function(0x00298578, level_unlocked);
    /* KZ_DEBUG_MENU=1 (testing): the pause menu's own Debug Options (God
     * Mode, Victory Status, Hit Info, the lock and spread circles), which it
     * offers when the build is not a submission one. IsSubmission's script
     * binding is the member function 0x002AE1C4 (return 1; IsCDBuild beside it
     * returns 0), reached through its pointer at 0x00549BC0. */
    if (getenv("KZ_DEBUG_MENU")) ppu_register_function(0x002AE1C4, not_submission);
    kz_pc_options_register();
    kz_route_register();   /* route.cpp: KZ_ROUTE_REC / KZ_ROUTE_PLAY */
    /* MultiStream (SPU image 3) decodes the music with the firmware's MP3
     * decoder, flashMP3.pic, which the game reads from dev_flash and loads at
     * LS 0x1A880 (its entry, 0x74A0 in the file, is called at LS 0x21D20).
     * The call goes to native/flashmp3.c (minimp3) unless KZ_MP3_PIC=1 (default
     * off) asks for the firmware's own decoder, which lift.sh lifts at that base:
     * the first registration of an address wins. Without either the audio task
     * ends at the first MP3 and every sound stops. */
    if (kzmp3_spu_recomp_register && !getenv("KZ_NO_MP3")) {
        spu_begin_image(3);
        if (!getenv("KZ_MP3_PIC")) spu_register_function(0x21D20, kz_flashmp3_entry);
        kzmp3_spu_recomp_register();
        spu_begin_image(0);
    } else fprintf(stderr, "[kz] flashMP3.pic not lifted (lift.sh): no music or sound after the first MP3\n");
}
