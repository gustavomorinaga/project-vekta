/* Patch 4017 (port issue #121, "the mouse flicks"): motion that must not turn the view never reaches the mouse
 * look. Motion SDL counted while the window was out of focus is dropped when the focus comes back, counts the title
 * did not take are dropped when a menu frees the pointer, and the first delta after the pointer is captured again
 * (back in play, or a click into the window) is dropped. Motion in play still goes through, whole.
 *
 * Built by test_pad_mouse_drop.sh against the patched tree. No window and no mouse: the test defines the SDL calls
 * the backend makes (they take precedence over libSDL2's), with a relative-motion counter it fills itself. */
#define SDL_MAIN_HANDLED
#include "cellPad.c"   /* from the patched tree, -I $PS3RECOMP_DIR/libs/input */
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

uint8_t* vm_base;
uint32_t ppu_vm_size;
int g_resv_store_active;
uint32_t g_ww_lo, g_ww_hi;
void ppu_resv_break_store(uint64_t a) { (void)a; }
void ps3_ww_report_inline(uint32_t a, uint64_t v, int w) { (void)a; (void)v; (void)w; }
int spu_coh_is_reserved(uint32_t a) { (void)a; return 0; }
void spu_coh_notify_write(uint32_t a) { (void)a; }
void spu_lockline_lock(void) {}
void spu_lockline_unlock(void) {}
u32 cellGcm_flip_request_count(void) { return 0; }

/* ---- the fake SDL: a focus flag, relative mode and the motion SDL has counted ---- */
static Uint8 s_keys[SDL_NUM_SCANCODES];
static int s_focus, s_rel, s_mdx, s_mdy;
int SDL_NumJoysticks(void) { return 0; }
int SDL_PeepEvents(SDL_Event* ev, int n, SDL_eventaction a, Uint32 lo, Uint32 hi) { (void)ev; (void)n; (void)a; (void)lo; (void)hi; return 0; }
Uint32 SDL_WasInit(Uint32 flags) { return flags; }
int SDL_InitSubSystem(Uint32 flags) { (void)flags; return 0; }
const char* SDL_GetError(void) { return "fake"; }
void SDL_GameControllerUpdate(void) {}
const Uint8* SDL_GetKeyboardState(int* n) { if (n) *n = SDL_NUM_SCANCODES; return s_keys; }
SDL_Window* SDL_GetKeyboardFocus(void) { return s_focus ? (SDL_Window*)&s_keys : NULL; }
SDL_Window* SDL_GetMouseFocus(void) { return NULL; }
Uint32 SDL_GetMouseState(int* x, int* y) { if (x) *x = 0; if (y) *y = 0; return 0; }
SDL_bool SDL_GetRelativeMouseMode(void) { return s_rel ? SDL_TRUE : SDL_FALSE; }
int SDL_SetRelativeMouseMode(SDL_bool on) { s_rel = on; return 0; }
Uint32 SDL_GetRelativeMouseState(int* x, int* y)
{
    if (x) *x = s_mdx;
    if (y) *y = s_mdy;
    s_mdx = s_mdy = 0;
    return 0;
}

static int fails;
static void poll(void) { usleep((PAD_POLL_INTERVAL_MS + 2) * 1000); pad_poll_backend(); }
/* what the next mouse-look tick takes */
static void expect_take(int want_x, int want_y, const char* what)
{
    int x, y;
    pad_mouse_take(&x, &y);
    if (x != want_x || y != want_y) { fprintf(stderr, "FAIL: %s: took %d,%d, want %d,%d\n", what, x, y, want_x, want_y); fails++; }
    else printf("ok   %s\n", what);
}

int main(void)
{
    vm_base = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (vm_base == MAP_FAILED || cellPadInit(4) != CELL_OK) { fprintf(stderr, "FAIL: setup\n"); return 1; }
    g_pad_mouse_direct = 1;
    s_focus = 1; s_rel = 1;
    poll(); poll(); poll();                 /* the first two polls with the focus drop what came before */
    expect_take(0, 0, "nothing moved: nothing taken");

    s_mdx = 10; s_mdy = -5; poll();
    expect_take(10, -5, "motion in play goes through");
    s_mdx = 3; poll(); s_mdx = 4; poll();
    expect_take(7, 0, "motion between two ticks adds up");

    /* Out of focus: SDL counts the pointer crossing the window (or arriving from another monitor) and nobody reads
     * it; when the focus comes back that count must not become a turn. */
    s_focus = 0; poll(); s_mdx = 300; s_mdy = 40; poll();
    s_focus = 1; poll();
    expect_take(0, 0, "motion counted out of focus is dropped when the focus comes back");
    s_mdx = 5; poll();
    expect_take(0, 0, "and the poll after it");
    s_mdx = 6; poll();
    expect_take(6, 0, "after the focus is back, motion goes through");

    /* Counts left untaken when a menu comes up (the look stops ticking) are not handed to play afterwards. */
    s_mdx = 50; poll();
    g_pad_mouse_ui = 1; poll();
    expect_take(0, 0, "counts left when a menu frees the pointer are dropped");
    if (s_rel) { fprintf(stderr, "FAIL: a menu leaves relative mode on\n"); fails++; }

    /* Back in play: the pointer is captured again, and the delta of the capture itself (a warp) is dropped. */
    g_pad_mouse_ui = 0; s_mdx = -400; poll();
    expect_take(0, 0, "back in play, the first delta of the capture is dropped");
    if (!s_rel) { fprintf(stderr, "FAIL: back in play, relative mode is off\n"); fails++; }
    s_mdx = -400; poll();
    expect_take(0, 0, "and the poll after it (a warp that lands late)");
    s_mdx = 9; s_mdy = 2; poll();
    expect_take(9, 2, "then motion goes through again");

    if (fails) return 1;
    puts("test_pad_mouse_drop: ok");
    return 0;
}
