/*
 * Killzone 1 HD -- PC-only behaviour on top of the game: mouse look with its
 * own sensitivity, and the options for it in the game's own Controls menu.
 *
 * Mouse look. func_00291FA8 is the player controller's per-tick update: it
 * reads the right stick, scales it by YawSensitivity/PitchSensitivity
 * (profile +0x140/+0x144), runs it through the game's response curve and turn
 * ramp, and leaves the turn rates at +0xA8 (yaw) and +0xAC (pitch), which the
 * player update integrates at 2.27 rad/s per unit (measured: rate 0.92 turns
 * once in 3 s). +0x3C is the yaw rate before a 0.93 scale, and a zero there
 * stops the turn. The mouse adds its own rate after all that, so the game's
 * sliders and curve stay the controller's.
 *
 * Options. The game's Pause > Controls > Advanced Settings menu is Lua
 * (MenuScript.Pause): sliders bound to reflected attributes of the
 * ControllerConfig class ('ControllerConfig.YawSensitivity'). Three attributes
 * are added to that class here (MouseYawSensitivity, MousePitchSensitivity,
 * MouseInvert) with host getters and setters, and a few lines of Lua run in the
 * menu's state put them in the list. The values live in kz1hd_pc.ini in the
 * VFS directory, not in the game's save.
 *
 * PC screen. Options > PC (frontend) and Pause > PC: display mode,
 * resolution, V-Sync, frame-rate limit, controllers and the mouse, as
 * attributes of the game system object ("PC settings.*"), applied at once
 * (window and swapchain through the Vulkan backend, the game clock through
 * main.cpp) and kept in kz1hd_pc.ini.
 *
 * PAD_MOUSE_STICK=1 keeps the mouse on the right stick instead (cellPad).
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>
#include <thread>
#include <map>
#include <string>
#include <cctype>
#include <algorithm>
#include <vector>

#include "ppu_recomp.h"
#include "view.h"

extern "C" {
extern uint8_t* vm_base;
extern volatile int g_pad_mouse_direct;
extern volatile int g_pad_mouse_ui;
extern volatile int g_kz_tick_hz;           /* main.cpp: the game clock's rate */
extern volatile double g_kz_guest_vblank_hz;
extern volatile double g_kz_flip_hz;
void pad_mouse_take(int* dx, int* dy);
void pad_mouse_ui_state(int* x, int* y, int* w, int* h, unsigned* buttons, int* wheel);
void pad_inject(unsigned mask, int reads);
void ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));
extern PPU_THREAD_LOCAL void (*g_trampoline_fn)(void*);
extern PPU_THREAD_LOCAL ppu_context* g_active_ctx;
}

static uint32_t rd32(uint32_t ea) { uint32_t v; memcpy(&v, vm_base + ea, 4); return __builtin_bswap32(v); }
static void wr32(uint32_t ea, uint32_t v) { v = __builtin_bswap32(v); memcpy(vm_base + ea, &v, 4); }
static float rdf(uint32_t ea) { uint32_t v = rd32(ea); float f; memcpy(&f, &v, 4); return f; }
static void wrf(uint32_t ea, float f) { uint32_t v; memcpy(&v, &f, 4); wr32(ea, v); }

/* ---- settings ---------------------------------------------------------- */

static const char* kIni = "kz1hd_pc.ini";   /* in the VFS directory (tools/run.sh and play.sh cd there) */
/* Slider positions 0..1, as the game's sliders store them; 0.5 = 1x. */
static float s_yaw = 0.5f, s_pitch = 0.5f;
static int   s_invert;

/* kz1hd_pc.ini holds key=value lines; every key is kept, whoever owns it. */
static std::map<std::string, std::string> s_ini;
static void ini_read(void)
{
    s_ini.clear();
    if (FILE* f = fopen(kIni, "r")) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            char* eq = strchr(line, '=');
            if (!eq || line[0] == '#') continue;
            *eq = 0;
            std::string k = line, v = eq + 1;
            while (!k.empty() && isspace((unsigned char)k.back())) k.pop_back();
            while (!k.empty() && isspace((unsigned char)k.front())) k.erase(0, 1);
            while (!v.empty() && isspace((unsigned char)v.back())) v.pop_back();
            while (!v.empty() && isspace((unsigned char)v.front())) v.erase(0, 1);
            if (!k.empty()) s_ini[k] = v;
        }
        fclose(f);
    }
}
static void ini_write(void)
{
    if (FILE* f = fopen(kIni, "w")) {
        for (auto& kv : s_ini) fprintf(f, "%s=%s\n", kv.first.c_str(), kv.second.c_str());
        fclose(f);
    }
}
static void ini_load(void)
{
    ini_read();
    auto num = [](const char* k, float d) { auto it = s_ini.find(k); return it == s_ini.end() ? d : (float)atof(it->second.c_str()); };
    s_yaw = num("mouse_yaw", s_yaw); s_pitch = num("mouse_pitch", s_pitch); s_invert = num("mouse_invert", 0) != 0;
}
static void ini_save(void)
{
    char b[32];
    snprintf(b, sizeof b, "%.4f", s_yaw);   s_ini["mouse_yaw"] = b;
    snprintf(b, sizeof b, "%.4f", s_pitch); s_ini["mouse_pitch"] = b;
    s_ini["mouse_invert"] = s_invert ? "1" : "0";
    ini_write();
}

/* ---- display and frame rate -------------------------------------------- *
 *
 * Read once at start, before the window and the game clock: each becomes the
 * environment the runtime reads (an environment variable already set wins).
 *   display    = window | borderless | fullscreen
 *   resolution = WxH (the window's size, or the fullscreen mode)
 *   refresh    = Hz of the fullscreen mode (0 = the display's)
 *   vsync      = on | off | adaptive | fifo   (on: no tearing; fifo: for a VRR display)
 *   fps_limit  = 30 | 60 | 144   (the game clock: 30 is the original; 144 runs
 *                                 ~67-80 fps, the PPU's limit)
 *   controllers = merged | separate   (merged: any controller plays as player 1)
 *   scale      = 50 | 75 | 100 | 150 | 200   (internal resolution, % of the window's; at the
 *                                             next start)
 *   widescreen = on | off   (on: the picture widens to the window's aspect, the 2D in the
 *                            16:9 middle; off: a 16:9 picture with bars at the sides)
 *   fov        = the vertical field of view in degrees, 40-90 (the game's is 56.25)
 *   hud_size   = 0.5-1.5   (1: the game's)
 *   hud_position = 0-1     (0: the HUD in the 16:9 middle, 1: at the screen's edges)
 *   overlay    = off | mangohud   (MangoHUD's statistics over the game; at the next
 *                                  start: the layer loads with the Vulkan instance)
 *   intro      = on | off   (the opening movie at start-up; off: skipped, KZ_SKIP_INTRO)
 *   anisotropic = game | off | 2 | 4 | 8 | 16   (game: what each texture asks for, as on
 *                                             the PS3; a number raises mipmapped textures
 *                                             to it; at the next start, PS3RECOMP_VK_ANISO)
 *   upscaler   = linear | fsr   (the picture to the window: a linear blit, or AMD FSR 1,
 *                                which sharpens with RCAS at the same size too)
 *   sharpness  = 0-2   (FSR's RCAS in stops: 0 the sharpest; AMD's default 0.2)
 *   speakers   = 2 | 6 | 8   (stereo, 5.1 or 7.1: the device the sound goes to, and what
 *                             the game is told it plays to, so its own mixer fills the
 *                             surround channels; at the next start, PS3RECOMP_AUDIO_CHANNELS)
 *   language   = system | en | en-gb | fr | es   (voices, subtitles and menus: the console
 *                             language the game is told, PS3_LANG; system = the host's, and
 *                             English for a language the game does not have)
 *   reflex     = off | on | boost   (NVIDIA Reflex, VK_NV_low_latency2: the game's thread
 *                             waits after each flip until the driver wants the next frame;
 *                             boost also keeps the GPU's clocks up. PS3RECOMP_VK_REFLEX,
 *                             always set so the device gets the extension and the row
 *                             applies at once; nothing without an NVIDIA GPU) */
static const char* present_mode(const std::string& v);   /* below, with the PC screen */
static const struct { const char* key; const char* def; } kVideo[] = {
    { "display", "window" }, { "resolution", "1280x720" }, { "refresh", "0" },
    { "vsync", "on" }, { "fps_limit", "60" }, { "controllers", "merged" }, { "scale", "100" },
    { "overlay", "off" }, { "widescreen", "on" }, { "fov", "56.25" }, { "hud_size", "1" },
    { "hud_position", "1" }, { "intro", "on" }, { "anisotropic", "game" },
    { "upscaler", "linear" }, { "sharpness", "0.2" }, { "speakers", "2" }, { "language", "system" },
    { "reflex", "off" },
};
/* MangoHUD with a preset for recording the game's progress: frame rate and
 * frame time, CPU per core, GPU, memory, resolution and driver. A
 * ~/.config/MangoHud/MangoHud.conf of the user's own (or MANGOHUD_CONFIG)
 * wins over it. */
static void overlay_env(void)
{
#ifdef _WIN32
    return;   /* no MangoHUD on Windows; under Proton the layer is the Linux side's (mangohud %command%) */
#endif
    if (s_ini["overlay"] != "mangohud" && !getenv("KZ_MANGOHUD")) return;
    setenv("MANGOHUD", "1", 0);
    const char* home = getenv("HOME");
    const std::string conf = std::string(home ? home : "") + "/.config/MangoHud/MangoHud.conf";
    if (FILE* f = fopen(conf.c_str(), "r")) { fclose(f); return; }
    setenv("MANGOHUD_CONFIG",
           "fps,frametime,frame_timing,cpu_stats,core_load,cpu_temp,gpu_stats,gpu_temp,vram,ram,"
           "resolution,vulkan_driver,engine_version,position=top-left,font_size=20,background_alpha=0.4", 0);
}
/* ---- the picture: widescreen, FOV and the HUD (view.h) ----------------- */

extern "C" struct KzView kz_view = { 0.0f, 0.0f, 1.0f, 1.0f };
extern "C" volatile unsigned kz_view_3d = 0, kz_view_menu = 0;
extern "C" {
void rsx_vulkan_set_stretch(int on);
void rsx_vulkan_set_fsr(int on, float sharpness);
void rsx_vulkan_set_reflex(int mode);
int  rsx_vulkan_reflex_available(void);
int  rsx_vulkan_output_size(int* w, int* h);
}
static bool s_widescreen = true;
static void view_load(void)
{
    s_widescreen = s_ini["widescreen"] != "off";
    kz_view.fov = std::clamp((float)atof(s_ini["fov"].c_str()), 40.0f, 90.0f);
    if (fabsf(kz_view.fov - 56.25f) < 0.01f) kz_view.fov = 0.0f;   /* the game's own */
    kz_view.hud_scale = std::clamp((float)atof(s_ini["hud_size"].c_str()), 0.5f, 1.5f);
    kz_view.hud_pos = std::clamp((float)atof(s_ini["hud_position"].c_str()), 0.0f, 1.0f);
}
/* Every frame (main.cpp): the cameras' aspect follows the window's. A window
 * no wider than 16:9 keeps the game's picture, with bars. */
static long long now_ms(void);
extern "C" void kz_view_update(void)
{
    int w, h;
    float a = 0.0f;
    if (s_widescreen && rsx_vulkan_output_size(&w, &h)) a = (float)w / (float)h;
    if (!(a > 16.0f / 9.0f + 0.01f)) a = 0.0f;
    /* A 2D screen (no 3D for 200 ms: menus, movies, loading) or a menu over
     * the game (the pause menu, a character on the frontend, Failed) is shown
     * as the game made it, 16:9 with bars: its layout has pieces just off a
     * 16:9 screen that widening would show. */
    static unsigned last_3d, last_menu; static long long last_3d_ms, last_menu_ms;
    const long long t = now_ms();
    if (kz_view_3d != last_3d) { last_3d = kz_view_3d; last_3d_ms = t; }
    if (kz_view_menu != last_menu) { last_menu = kz_view_menu; last_menu_ms = t; }
    static int log = getenv("KZ_CAM_LOG") ? 1 : 0; static long long next_log;
    if (log && t >= next_log) { next_log = t + 5000; fprintf(stderr, "[kz-view] 3d %u menu %u\n", kz_view_3d, kz_view_menu); }
    const bool flat_now = t - last_3d_ms > 200 || t - last_menu_ms < 200;
    /* into 16:9 at once, back to the full width after half a second of 3D
     * without a menu: a screen does not flicker between the two */
    static bool flat; static long long wide_since;
    if (flat_now) { flat = true; wide_since = 0; }
    else if (flat) { if (!wide_since) wide_since = t; if (t - wide_since >= 500) flat = false; }
    if (flat) a = 0.0f;
    if (a != kz_view.aspect) {
        kz_view.aspect = a;
        rsx_vulkan_set_stretch(a > 0.0f);
    }
}

extern "C" void kz_pc_settings_env(void)
{
    ini_read();
    bool added = false;
    for (auto& d : kVideo) if (!s_ini.count(d.key)) { s_ini[d.key] = d.def; added = true; }
    if (added) ini_write();
    setenv("PS3RECOMP_VK_DISPLAY", s_ini["display"].c_str(), 0);
    setenv("PS3RECOMP_VK_RESOLUTION", s_ini["resolution"].c_str(), 0);
    if (atoi(s_ini["refresh"].c_str()) > 0) setenv("PS3RECOMP_VK_REFRESH", s_ini["refresh"].c_str(), 0);
    const std::string& vs = s_ini["vsync"];
    setenv("PS3RECOMP_VK_PRESENT", present_mode(vs), 0);
    const int fps = atoi(s_ini["fps_limit"].c_str());
    /* KZ_VBLANK_HZ, the flip cadence: the FIFO drains one frame per tick of
     * it, so at 144 it held the 144 Hz clock to ~72 fps (a frame over one tick
     * waits for the next two). In play at 144 menu_mouse raises it to 1000,
     * every frame through at once (~90-115 fps in Level 1). At 60 or 30 it
     * stays 144: faster flips only show the same game tick again -- except
     * with VRR (vsync = fifo), where the display follows the flips: on a
     * 144 Hz cadence 60 fps came out as frames 2 and 3 ticks apart (the
     * monitor swinging between 72 and 144 Hz), so there it is the PS3's
     * 59.94 and every frame is shown for the same time. */
    if (fps >= 100) { setenv("KZ_TICK_HZ", "144", 0); setenv("KZ_GUEST_VBLANK_HZ", "144", 0); }
    else if (fps <= 30) { setenv("KZ_TICK_HZ", "30", 0); setenv("KZ_GUEST_VBLANK_HZ", "59.94", 0); }
    else { setenv("KZ_TICK_HZ", "60", 0); setenv("KZ_GUEST_VBLANK_HZ", "59.94", 0); }
    setenv("KZ_VBLANK_HZ", fps < 100 && vs == "fifo" ? "59.94" : "144", 0);
    setenv("PAD_MERGE", s_ini["controllers"] == "separate" ? "0" : "1", 0);
    { char b[16]; snprintf(b, sizeof b, "%.2f", atoi(s_ini["scale"].c_str()) / 100.0);
      setenv("PS3RECOMP_VK_SCALE", b, 0); }
    /* the frame at the window's size: anamorphic to its aspect for widescreen */
    setenv("PS3RECOMP_VK_SCALE_FIT", s_ini["widescreen"] == "off" ? "fit" : "fill", 0);
    if (s_ini["intro"] == "off") setenv("KZ_SKIP_INTRO", "1", 0);   /* sail.cpp */
    { const std::string& a = s_ini["anisotropic"];
      setenv("PS3RECOMP_VK_ANISO", a == "off" ? "1" : a == "game" ? "0" : a.c_str(), 0); }
    setenv("PS3RECOMP_VK_FSR", s_ini["upscaler"] == "fsr" ? "1" : "0", 0);
    setenv("PS3RECOMP_VK_FSR_SHARPNESS", s_ini["sharpness"].c_str(), 0);
    setenv("PS3RECOMP_AUDIO_CHANNELS", s_ini["speakers"].c_str(), 0);
    if (s_ini["language"] != "system") setenv("PS3_LANG", s_ini["language"].c_str(), 0);
    setenv("PS3RECOMP_VK_REFLEX", s_ini["reflex"] == "boost" ? "2" : s_ini["reflex"] == "on" ? "1" : "0", 0);
    overlay_env();
    view_load();
}

/* Slider position to a multiplier: 0 = 0.1x, 0.5 = 1x, 1 = 10x. */
static float sens(float slider) { return 0.1f * powf(100.0f, slider); }

/* ---- mouse look -------------------------------------------------------- */

static float g_look_k;
static int   g_look_log;
static void kz_pc_inject_attrs(ppu_context* ctx);

static long long now_ms(void)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
static long long s_last_play_ms;   /* the last player tick: the mouse looks while recent */

static void player_look(ppu_context* ctx)
{
    s_last_play_ms = now_ms();
    kz_pc_inject_attrs(ctx);
    const uint32_t obj = (uint32_t)ctx->gpr[3];
    func_00291FA8(ctx);
    int dx, dy;
    pad_mouse_take(&dx, &dy);
    if (!dx && !dy) return;
    const float k = g_look_k * (float)(g_kz_tick_hz > 0 ? g_kz_tick_hz : 30);   /* per tick */
    const float yaw = rdf(obj + 0xA8), pitch = rdf(obj + 0xAC);
    wrf(obj + 0xA8, yaw - (float)dx * k * sens(s_yaw));
    wrf(obj + 0x3C, rdf(obj + 0xA8) / 0.93f);
    wrf(obj + 0xAC, pitch + (float)(s_invert ? dy : -dy) * k * sens(s_pitch));
    if (g_look_log) { static int n; if (n++ % 30 == 0)
        fprintf(stderr, "[kz-look] dx=%d dy=%d rates %.4f %.4f -> %.4f %.4f\n", dx, dy,
                yaw, pitch, rdf(obj + 0xA8), rdf(obj + 0xAC)); }
}

/* ---- guest calls from a hook ------------------------------------------ */

/* Runs a lifted function on a copy of the caller's context, with the stack
 * pointer 16 KB below the caller's: the hook's own frame stays untouched. */
static uint64_t guest(ppu_context* base, void (*fn)(ppu_context*),
                      uint64_t a0, uint64_t a1 = 0, uint64_t a2 = 0, uint64_t a3 = 0, uint64_t a4 = 0)
{
    ppu_context c = *base;
    c.gpr[1] = (base->gpr[1] - 0x4000) & ~0xFull;
    c.gpr[3] = a0; c.gpr[4] = a1; c.gpr[5] = a2; c.gpr[6] = a3; c.gpr[7] = a4;
    c.lr = 0;
    ppu_context* const active = g_active_ctx;   /* store-conditionals check the active context */
    g_active_ctx = &c;
    fn(&c);
    while (g_trampoline_fn) { void (*t)(void*) = g_trampoline_fn; g_trampoline_fn = 0; t(&c); }
    g_active_ctx = active;
    return c.gpr[3];
}

static uint32_t guest_alloc(ppu_context* ctx, uint32_t n) { return (uint32_t)guest(ctx, func_00063C8C, n); }
static uint32_t guest_str(ppu_context* ctx, const char* s)
{
    const uint32_t n = (uint32_t)strlen(s) + 1, ea = guest_alloc(ctx, n);
    if (ea) memcpy(vm_base + ea, s, n);
    return ea;
}

/* ---- the attributes ---------------------------------------------------- */

/* Getters take (this, out*); setters (this, in*, how) and only store for how == 2,
 * like the game's own (func_00298AD8 / func_00298AF4). */
static void attr_log(const char* w, ppu_context* c)
{
    if (g_look_log) fprintf(stderr, "[kz-attr] %s r3=%08X r4=%08X r5=%d\n", w, (uint32_t)c->gpr[3], (uint32_t)c->gpr[4], (int)c->gpr[5]);
}
static void get_yaw(ppu_context* c) { attr_log("get_yaw", c); wrf((uint32_t)c->gpr[4], s_yaw); }
static void get_pitch(ppu_context* c) { attr_log("get_pitch", c); wrf((uint32_t)c->gpr[4], s_pitch); }
static void get_inv(ppu_context* c) { attr_log("get_inv", c); vm_base[(uint32_t)c->gpr[4]] = (uint8_t)s_invert; }
static void set_yaw(ppu_context* c) { attr_log("set_yaw", c); if ((int32_t)c->gpr[5] == 2) { s_yaw = rdf((uint32_t)c->gpr[4]); ini_save(); } }
static void set_pitch(ppu_context* c) { attr_log("set_pitch", c); if ((int32_t)c->gpr[5] == 2) { s_pitch = rdf((uint32_t)c->gpr[4]); ini_save(); } }
static void set_inv(ppu_context* c) { attr_log("set_inv", c); if ((int32_t)c->gpr[5] == 2) { s_invert = vm_base[(uint32_t)c->gpr[4]] != 0; ini_save(); } }

/* The PC settings (kz1hd_pc.ini), as float attributes whose values are the
 * choices' indices, changed while running. */
extern "C" {
void rsx_vulkan_set_display(const char* mode, int w, int h, int hz);
void rsx_vulkan_set_present(const char* mode);
int  rsx_vulkan_display_sizes(int (*out)[2], int max);
void pad_set_merge(int on);
}
static const char* const kDisplays[] = { "window", "borderless", "fullscreen" };
static const char* const kVsyncs[]   = { "on", "off", "adaptive", "fifo" };
/* on: mailbox where the driver has it (no tearing, and a frame that misses a
 * refresh is not held to the next one, which halved the frame rate at 144),
 * else FIFO; fifo: strict FIFO, what a VRR display follows. */
static const char* present_mode(const std::string& v)
{
    return v == "off" ? "immediate" : v == "adaptive" ? "relaxed" : v == "fifo" ? "fifo" : "mailbox";
}
static const int kFps[] = { 30, 60, 144 };
static std::vector<std::pair<int, int>> s_res;   /* the resolutions offered */

static int pick(const std::string& v, const char* const* names, int n) { for (int i = 0; i < n; i++) if (v == names[i]) return i; return 0; }
static void res_list(void)
{
    if (!s_res.empty()) return;
    int sz[32][2];
    const int n = rsx_vulkan_display_sizes(sz, 32);
    for (int i = 0; i < n; i++) s_res.push_back({ sz[i][0], sz[i][1] });
    static const int common[][2] = { {3840,2160},{2560,1440},{1920,1080},{1600,900},{1280,720} };
    if (s_res.empty()) for (auto& c : common) s_res.push_back({ c[0], c[1] });
    int w, h;
    if (sscanf(s_ini["resolution"].c_str(), "%dx%d", &w, &h) == 2 &&
        std::find(s_res.begin(), s_res.end(), std::make_pair(w, h)) == s_res.end()) {
        s_res.push_back({ w, h });
        std::sort(s_res.begin(), s_res.end(), [](auto& a, auto& b) { return a.first * a.second > b.first * b.second; });
    }
    if (s_res.size() > 16) s_res.resize(16);
}
static int res_index(void)
{
    int w, h;
    if (sscanf(s_ini["resolution"].c_str(), "%dx%d", &w, &h) != 2) return 0;
    for (size_t i = 0; i < s_res.size(); i++) if (s_res[i] == std::make_pair(w, h)) return (int)i;
    return 0;
}
static void apply_display(void)
{
    int w = 1280, h = 720;
    sscanf(s_ini["resolution"].c_str(), "%dx%d", &w, &h);
    rsx_vulkan_set_display(s_ini["display"].c_str(), w, h, atoi(s_ini["refresh"].c_str()));
}
static void apply_fps(int fps)
{
    g_kz_tick_hz = fps >= 100 ? 144 : fps <= 30 ? 30 : 60;
    g_kz_guest_vblank_hz = fps >= 100 ? 144.0 : 59.94;
    g_kz_flip_hz = fps < 100 && s_ini["vsync"] == "fifo" ? 59.94 : 144.0;   /* VRR: see kz_pc_settings_env; menu_mouse raises it in play at 144 */
}
static float in_f(ppu_context* c) { return rdf((uint32_t)c->gpr[4]); }
static void get_display(ppu_context* c) { attr_log("get_display", c); wrf((uint32_t)c->gpr[4], (float)pick(s_ini["display"], kDisplays, 3)); }
static void set_display(ppu_context* c) { attr_log("set_display", c); if ((int32_t)c->gpr[5] != 2) return;
    s_ini["display"] = kDisplays[std::clamp((int)lroundf(in_f(c)), 0, 2)]; ini_write(); apply_display(); }
static void get_res(ppu_context* c) { attr_log("get_res", c); res_list(); wrf((uint32_t)c->gpr[4], (float)res_index()); }
static void set_res(ppu_context* c) { attr_log("set_res", c); if ((int32_t)c->gpr[5] != 2) return; res_list();
    const int i = std::clamp((int)lroundf(in_f(c)), 0, (int)s_res.size() - 1);
    char b[32]; snprintf(b, sizeof b, "%dx%d", s_res[i].first, s_res[i].second);
    if (s_ini["resolution"] == b) return;
    s_ini["resolution"] = b; ini_write(); apply_display(); }
static void get_vsync(ppu_context* c) { attr_log("get_vsync", c); wrf((uint32_t)c->gpr[4], (float)pick(s_ini["vsync"], kVsyncs, 4)); }
static void set_vsync(ppu_context* c) { attr_log("set_vsync", c); if ((int32_t)c->gpr[5] != 2) return;
    const int i = std::clamp((int)lroundf(in_f(c)), 0, 3);
    s_ini["vsync"] = kVsyncs[i]; ini_write();
    rsx_vulkan_set_present(present_mode(kVsyncs[i]));
    apply_fps(atoi(s_ini["fps_limit"].c_str())); }
static void get_fps(ppu_context* c) { attr_log("get_fps", c);
    const int f = atoi(s_ini["fps_limit"].c_str());
    wrf((uint32_t)c->gpr[4], (float)(f >= 100 ? 2 : f <= 30 ? 0 : 1)); }
static void set_fps(ppu_context* c) { attr_log("set_fps", c); if ((int32_t)c->gpr[5] != 2) return;
    const int f = kFps[std::clamp((int)lroundf(in_f(c)), 0, 2)];
    s_ini["fps_limit"] = std::to_string(f); ini_write(); apply_fps(f); }
static const int kScales[] = { 50, 75, 100, 150, 200 };
static void get_scale(ppu_context* c) { attr_log("get_scale", c);
    const int v = atoi(s_ini["scale"].c_str()); int i = 2;
    for (int k = 0; k < 5; k++) if (kScales[k] == v) i = k;
    wrf((uint32_t)c->gpr[4], (float)i); }
static void set_scale(ppu_context* c) { attr_log("set_scale", c); if ((int32_t)c->gpr[5] != 2) return;
    s_ini["scale"] = std::to_string(kScales[std::clamp((int)lroundf(in_f(c)), 0, 4)]); ini_write(); }
static void get_overlay(ppu_context* c) { attr_log("get_overlay", c); vm_base[(uint32_t)c->gpr[4]] = s_ini["overlay"] == "mangohud"; }
static void set_overlay(ppu_context* c) { attr_log("set_overlay", c); if ((int32_t)c->gpr[5] != 2) return;
    s_ini["overlay"] = vm_base[(uint32_t)c->gpr[4]] ? "mangohud" : "off"; ini_write(); }
static const char* const kAniso[] = { "game", "off", "2", "4", "8", "16" };
static void get_aniso(ppu_context* c) { attr_log("get_aniso", c); int i = 0;
    for (int k = 0; k < 6; k++) if (s_ini["anisotropic"] == kAniso[k]) i = k;
    wrf((uint32_t)c->gpr[4], (float)i); }
static void set_aniso(ppu_context* c) { attr_log("set_aniso", c); if ((int32_t)c->gpr[5] != 2) return;
    s_ini["anisotropic"] = kAniso[std::clamp((int)lroundf(in_f(c)), 0, 5)]; ini_write(); }
/* The upscaler and RCAS's sharpness apply at once (rsx_vulkan_set_fsr); the
 * slider runs from 2 stops (0) to the sharpest (1). */
static void fsr_apply(void) { rsx_vulkan_set_fsr(s_ini["upscaler"] == "fsr", (float)atof(s_ini["sharpness"].c_str())); }
static void get_upscaler(ppu_context* c) { attr_log("get_upscaler", c); wrf((uint32_t)c->gpr[4], s_ini["upscaler"] == "fsr" ? 1.0f : 0.0f); }
static void set_upscaler(ppu_context* c) { attr_log("set_upscaler", c); if ((int32_t)c->gpr[5] != 2) return;
    s_ini["upscaler"] = lroundf(in_f(c)) ? "fsr" : "linear"; ini_write(); fsr_apply(); }
static void get_sharp(ppu_context* c) { attr_log("get_sharp", c);
    wrf((uint32_t)c->gpr[4], 1.0f - std::clamp((float)atof(s_ini["sharpness"].c_str()), 0.0f, 2.0f) / 2.0f); }
static void set_sharp(ppu_context* c) { attr_log("set_sharp", c); if ((int32_t)c->gpr[5] != 2) return;
    char b[16]; snprintf(b, sizeof b, "%.2f", 2.0f * (1.0f - std::clamp(in_f(c), 0.0f, 1.0f))); s_ini["sharpness"] = b;
    ini_write(); fsr_apply(); }
static const char* const kSpeakers[] = { "2", "6", "8" };
static void get_speakers(ppu_context* c) { attr_log("get_speakers", c); int i = 0;
    for (int k = 0; k < 3; k++) if (s_ini["speakers"] == kSpeakers[k]) i = k;
    wrf((uint32_t)c->gpr[4], (float)i); }
static void set_speakers(ppu_context* c) { attr_log("set_speakers", c); if ((int32_t)c->gpr[5] != 2) return;
    s_ini["speakers"] = kSpeakers[std::clamp((int)lroundf(in_f(c)), 0, 2)]; ini_write(); }
static const char* const kLanguages[] = { "system", "en", "en-gb", "fr", "es" };
static void get_language(ppu_context* c) { attr_log("get_language", c); int i = 0;
    for (int k = 0; k < 5; k++) if (s_ini["language"] == kLanguages[k]) i = k;
    wrf((uint32_t)c->gpr[4], (float)i); }
static void set_language(ppu_context* c) { attr_log("set_language", c); if ((int32_t)c->gpr[5] != 2) return;
    s_ini["language"] = kLanguages[std::clamp((int)lroundf(in_f(c)), 0, 4)]; ini_write(); }
static const char* const kReflex[] = { "off", "on", "boost" };
static void get_reflex(ppu_context* c) { attr_log("get_reflex", c); int i = 0;
    for (int k = 0; k < 3; k++) if (s_ini["reflex"] == kReflex[k]) i = k;
    wrf((uint32_t)c->gpr[4], (float)i); }
static void set_reflex(ppu_context* c) { attr_log("set_reflex", c); if ((int32_t)c->gpr[5] != 2) return;
    const int i = std::clamp((int)lroundf(in_f(c)), 0, 2);
    s_ini["reflex"] = kReflex[i]; ini_write(); rsx_vulkan_set_reflex(i); }
/* Presets: a set of the options above, written at once (internal resolution
 * and anisotropy at the next start, the rest at once; reopen the screen to see
 * the rows follow). Custom: whatever does not match one. Original is the
 * PS3's: 30 fps, 16:9 with bars, its FOV and HUD, no upscaler; the others
 * keep the picture widescreen. */
static const struct { const char* name; const char* kv[8][2]; } kPresets[] = {
    { "original",    { { "fps_limit", "30" }, { "widescreen", "off" }, { "fov", "56.25" }, { "hud_size", "1" },
                       { "scale", "100" }, { "anisotropic", "game" }, { "upscaler", "linear" }, { nullptr, nullptr } } },
    { "performance", { { "fps_limit", "60" }, { "widescreen", "on" }, { "scale", "75" }, { "upscaler", "fsr" }, { "sharpness", "0.2" },
                       { "anisotropic", "game" }, { nullptr, nullptr } } },
    { "balanced",    { { "fps_limit", "60" }, { "widescreen", "on" }, { "scale", "100" }, { "upscaler", "fsr" }, { "sharpness", "0.2" },
                       { "anisotropic", "8" }, { nullptr, nullptr } } },
    { "quality",     { { "fps_limit", "144" }, { "widescreen", "on" }, { "scale", "150" }, { "upscaler", "linear" }, { "anisotropic", "16" },
                       { nullptr, nullptr } } },
};
static void get_preset(ppu_context* c) { attr_log("get_preset", c); int idx = 0;
    for (int k = 0; k < 4 && !idx; k++) {
        bool all = true;
        for (int i = 0; i < 8 && kPresets[k].kv[i][0]; i++) all = all && s_ini[kPresets[k].kv[i][0]] == kPresets[k].kv[i][1];
        if (all) idx = k + 1;
    }
    wrf((uint32_t)c->gpr[4], (float)idx); }
static void set_preset(ppu_context* c) { attr_log("set_preset", c); if ((int32_t)c->gpr[5] != 2) return;
    const int k = std::clamp((int)lroundf(in_f(c)), 0, 4) - 1;
    if (k < 0) return;
    for (int i = 0; i < 8 && kPresets[k].kv[i][0]; i++) s_ini[kPresets[k].kv[i][0]] = kPresets[k].kv[i][1];
    ini_write();
    apply_fps(atoi(s_ini["fps_limit"].c_str()));
    view_load();
    fsr_apply(); }
static void get_intro(ppu_context* c) { attr_log("get_intro", c); vm_base[(uint32_t)c->gpr[4]] = s_ini["intro"] != "off"; }
static void set_intro(ppu_context* c) { attr_log("set_intro", c); if ((int32_t)c->gpr[5] != 2) return;
    s_ini["intro"] = vm_base[(uint32_t)c->gpr[4]] ? "on" : "off"; ini_write(); }
static void get_wide(ppu_context* c) { attr_log("get_wide", c); vm_base[(uint32_t)c->gpr[4]] = s_widescreen; }
static void set_wide(ppu_context* c) { attr_log("set_wide", c); if ((int32_t)c->gpr[5] != 2) return;
    s_ini["widescreen"] = vm_base[(uint32_t)c->gpr[4]] ? "on" : "off"; ini_write(); view_load(); }
/* The sliders' positions, 0..1: FOV 40-90 degrees, HUD size 0.5-1.5, HUD position as is. */
static void set_num(const char* key, float v) { char b[32]; snprintf(b, sizeof b, "%.4g", v); s_ini[key] = b; ini_write(); view_load(); }
static void get_fov(ppu_context* c) { attr_log("get_fov", c);
    wrf((uint32_t)c->gpr[4], (std::clamp((float)atof(s_ini["fov"].c_str()), 40.0f, 90.0f) - 40.0f) / 50.0f); }
static void set_fov(ppu_context* c) { attr_log("set_fov", c); if ((int32_t)c->gpr[5] != 2) return;
    set_num("fov", 40.0f + 50.0f * std::clamp(in_f(c), 0.0f, 1.0f)); }
static void get_hud_size(ppu_context* c) { attr_log("get_hud_size", c); wrf((uint32_t)c->gpr[4], kz_view.hud_scale - 0.5f); }
static void set_hud_size(ppu_context* c) { attr_log("set_hud_size", c); if ((int32_t)c->gpr[5] != 2) return;
    set_num("hud_size", 0.5f + std::clamp(in_f(c), 0.0f, 1.0f)); }
static void get_hud_pos(ppu_context* c) { attr_log("get_hud_pos", c); wrf((uint32_t)c->gpr[4], kz_view.hud_pos); }
static void set_hud_pos(ppu_context* c) { attr_log("set_hud_pos", c); if ((int32_t)c->gpr[5] != 2) return;
    set_num("hud_position", std::clamp(in_f(c), 0.0f, 1.0f)); }
/* Quit: Options > Quit Game (the frontend) sets it; true ends the process (main.cpp). */
extern "C" void kz_quit(const char* why);
/* Ready: lua_binding's probe sets it when the state still has a menu
 * script to change (the name is from when the script set it itself). */
static bool s_lua_ready;
static void get_ready(ppu_context* c) { vm_base[(uint32_t)c->gpr[4]] = s_lua_ready; }
static void set_ready(ppu_context* c) { if ((int32_t)c->gpr[5] == 2 && vm_base[(uint32_t)c->gpr[4]]) s_lua_ready = true; }
/* Trace: the Lua below marks events with it (KZ_LUA_TRACE=1 logs them). */
static void get_trace(ppu_context* c) { vm_base[(uint32_t)c->gpr[4]] = 0; }
static void set_trace(ppu_context* c) {
    static const bool on = getenv("KZ_LUA_TRACE") != nullptr; static unsigned n;
    if (on && (int32_t)c->gpr[5] == 2) fprintf(stderr, "[kz-lua] trace %u at %lld ms\n", ++n, now_ms()); }
static void get_quit(ppu_context* c) { attr_log("get_quit", c); vm_base[(uint32_t)c->gpr[4]] = 0; }
static void set_quit(ppu_context* c) { attr_log("set_quit", c);
    if ((int32_t)c->gpr[5] == 2 && vm_base[(uint32_t)c->gpr[4]]) kz_quit("Quit Game"); }
static void get_pads(ppu_context* c) { attr_log("get_pads", c); wrf((uint32_t)c->gpr[4], s_ini["controllers"] == "separate" ? 1.0f : 0.0f); }
static void set_pads(ppu_context* c) { attr_log("set_pads", c); if ((int32_t)c->gpr[5] != 2) return;
    const bool sep = lroundf(in_f(c)) == 1;
    s_ini["controllers"] = sep ? "separate" : "merged"; ini_write(); pad_set_merge(!sep); }

static const uint32_t kClass      = 0x0131E4DC;   /* ControllerConfig's class (func_00501A70) */
static const uint32_t kSysClass   = 0x0131ECD0;   /* gGameSystem's class ("Audio settings" and the rest) */
static const uint32_t kCategory   = 0x00548880;   /* "ControllerConfig" */
static const uint32_t kFloatVtbl  = 0x00552FC8, kBoolVtbl = 0x00552728;
static const uint32_t kToc        = 0x005EE4A0;
static const uint32_t kFakeCode   = 0x00000100;   /* host functions, at addresses no code has */

static void kz_pc_inject_attrs(ppu_context* ctx)
{
    static bool done;
    if (done) return;
    done = true;
    /* The mouse on the player's profile (Pause > Controls > Advanced), and
     * everything on the game system object (gGameSystem, class 0x131ECD0,
     * whose attributes the menus read and write directly, like its "Audio
     * settings"): category "PC settings". The profile's attributes go
     * through the profile's own copy of the values instead. */
    static const struct { uint32_t cls; const char* name; uint32_t vtbl; void (*get)(ppu_context*); void (*set)(ppu_context*); } a[] = {
        { kClass,    "MouseYawSensitivity",   kFloatVtbl, get_yaw,     set_yaw },
        { kClass,    "MousePitchSensitivity", kFloatVtbl, get_pitch,   set_pitch },
        { kClass,    "MouseInvert",           kBoolVtbl,  get_inv,     set_inv },
        { kSysClass, "Display",               kFloatVtbl, get_display, set_display },
        { kSysClass, "Resolution",            kFloatVtbl, get_res,     set_res },
        { kSysClass, "VSync",                 kFloatVtbl, get_vsync,   set_vsync },
        { kSysClass, "FrameRate",             kFloatVtbl, get_fps,     set_fps },
        { kSysClass, "Controllers",           kFloatVtbl, get_pads,    set_pads },
        { kSysClass, "Scale",                 kFloatVtbl, get_scale,   set_scale },
        { kSysClass, "MouseYaw",              kFloatVtbl, get_yaw,     set_yaw },
        { kSysClass, "MousePitch",            kFloatVtbl, get_pitch,   set_pitch },
        { kSysClass, "MouseInvert",           kBoolVtbl,  get_inv,     set_inv },
        { kSysClass, "Overlay",               kBoolVtbl,  get_overlay, set_overlay },
        { kSysClass, "Quit",                  kBoolVtbl,  get_quit,    set_quit },
        { kSysClass, "Ready",                 kBoolVtbl,  get_ready,   set_ready },
        { kSysClass, "Trace",                 kBoolVtbl,  get_trace,   set_trace },
        { kSysClass, "Widescreen",            kBoolVtbl,  get_wide,    set_wide },
        { kSysClass, "FOV",                   kFloatVtbl, get_fov,     set_fov },
        { kSysClass, "HUDSize",               kFloatVtbl, get_hud_size, set_hud_size },
        { kSysClass, "HUDPosition",           kFloatVtbl, get_hud_pos, set_hud_pos },
        { kSysClass, "IntroMovie",            kBoolVtbl,  get_intro,   set_intro },
        { kSysClass, "Anisotropic",           kFloatVtbl, get_aniso,   set_aniso },
        { kSysClass, "Upscaler",              kFloatVtbl, get_upscaler, set_upscaler },
        { kSysClass, "Sharpness",             kFloatVtbl, get_sharp,   set_sharp },
        { kSysClass, "Speakers",              kFloatVtbl, get_speakers, set_speakers },
        { kSysClass, "Language",              kFloatVtbl, get_language, set_language },
        { kSysClass, "Reflex",                kFloatVtbl, get_reflex,  set_reflex },
        { kSysClass, "Preset",                kFloatVtbl, get_preset,  set_preset },
    };
    const uint32_t pc_cat = guest_str(ctx, "PC settings");
    for (unsigned i = 0; i < sizeof a / sizeof a[0]; i++) {
        const uint32_t cat = a[i].cls == kClass ? kCategory : pc_cat;
        const uint32_t getc = kFakeCode + 8 * i, setc = getc + 4;
        ppu_register_function(getc, a[i].get);
        ppu_register_function(setc, a[i].set);
        const uint32_t opd = guest_alloc(ctx, 16);
        wr32(opd, getc); wr32(opd + 4, kToc); wr32(opd + 8, setc); wr32(opd + 12, kToc);
        const uint32_t attr = guest_alloc(ctx, 0x28);
        if (!opd || !attr) return;
        guest(ctx, func_000A0F38, attr, cat, guest_str(ctx, a[i].name), (uint64_t)-1, 0);
        wr32(attr + 0x00, a[i].vtbl);
        wr32(attr + 0x14, 0);
        wr32(attr + 0x18, opd);     wr32(attr + 0x1C, 0);   /* getter {OPD, this adjust} */
        wr32(attr + 0x20, opd + 8); wr32(attr + 0x24, 0);   /* setter */
        guest(ctx, func_000A14C4, a[i].cls, attr);
        if (g_look_log) { fprintf(stderr, "[kz-attr] %s at %08X:", a[i].name, attr);
            for (int k = 0; k < 0x28; k += 4) fprintf(stderr, " %08X", rd32(attr + k)); fprintf(stderr, "\n"); }
    }
    fprintf(stderr, "[kz] PC options: attributes added (ControllerConfig, PC settings)\n");
}

/* ---- the menu ---------------------------------------------------------- */

/* Lua (4.0) run in every state the generic binding (func_00185100) is
 * called from; each part waits, by its guard, for the script it changes:
 *   - Pause > Controls > Advanced Settings: the list made ten rows long, with
 *     the mouse's sensitivity and invert added;
 *   - the pause menu: a "PC" item after Controls, opening the PC screen;
 *   - the frontend's Options: a "PC" item above Audio, the same screen.
 * The PC screen's rows are attributes added to ControllerConfig (above), on
 * the player's profile; the resolutions are the display's. */
static std::string kz_lua(void)
{
    res_list();
    std::string rv, rl;
    for (size_t i = 0; i < s_res.size(); i++) {
        rv += (i ? ";" : "") + std::to_string(i) + ".0";   /* a float attribute reads back as "1.0" */
        rl += (i ? ";" : "") + std::to_string(s_res[i].first) + "x" + std::to_string(s_res[i].second);
    }
    std::string L =
    "if not KZPCROWS then\n"
    " KZPCROWS = function(b)\n"
    "  local p = gGameSystem\n"
    "  b:SetItem(0, 0, gMenu:AddAttributeEditHD(0, 0, 0, 0, p, 'PC settings.Preset|0.0;1.0;2.0;3.0;4.0|Custom;Original (PS3);Performance;Balanced;Quality', 'Preset'))\n"
    "  b:SetItem(1, 0, gMenu:AddAttributeEditHD(0, 0, 0, 0, p, 'PC settings.Display|0.0;1.0;2.0|Window;Borderless;Fullscreen', 'Display Mode'))\n"
    "  b:SetItem(2, 0, gMenu:AddAttributeEditHD(0, 0, 0, 0, p, 'PC settings.Resolution|" + rv + "|" + rl + "', 'Resolution'))\n"
    "  b:SetItem(3, 0, gMenu:AddAttributeEditHD(0, 0, 0, 0, p, 'PC settings.Widescreen|true;false|$Conditions.On$;$Conditions.Off$', 'Widescreen'))\n"
    "  b:SetItem(4, 0, gMenu:AddAttributeSliderHD(0, 0, 0, 0, 5, 0.5, p, 'PC settings.FOV', 'Field of View'))\n"
    "  b:SetItem(5, 0, gMenu:AddAttributeSliderHD(0, 0, 0, 0, 5, 0.5, p, 'PC settings.HUDSize', 'HUD Size'))\n"
    "  b:SetItem(6, 0, gMenu:AddAttributeSliderHD(0, 0, 0, 0, 5, 0.5, p, 'PC settings.HUDPosition', 'HUD Position'))\n"
    "  b:SetItem(7, 0, gMenu:AddAttributeEditHD(0, 0, 0, 0, p, 'PC settings.VSync|0.0;1.0;2.0;3.0|On;Off;Adaptive;FIFO (VRR)', 'V-Sync'))\n"
    "  b:SetItem(8, 0, gMenu:AddAttributeEditHD(0, 0, 0, 0, p, 'PC settings.FrameRate|0.0;1.0;2.0|30;60;144', 'Frame Rate Limit'))\n"
    "  b:SetItem(9, 0, gMenu:AddAttributeEditHD(0, 0, 0, 0, p, 'PC settings.Reflex|0.0;1.0;2.0|$Conditions.Off$;$Conditions.On$;On + Boost', '" +
        std::string(rsx_vulkan_reflex_available() ? "NVIDIA Reflex" : "NVIDIA Reflex (not available)") + "'))\n"
    "  b:SetItem(10, 0, gMenu:AddAttributeEditHD(0, 0, 0, 0, p, 'PC settings.Controllers|0.0;1.0|Any is Player 1;One per Player', 'Controllers'))\n"
    "  b:SetItem(11, 0, gMenu:AddAttributeEditHD(0, 0, 0, 0, p, 'PC settings.Scale|0.0;1.0;2.0;3.0;4.0|50%;75%;100%;150%;200%', 'Internal Resolution (on restart)'))\n"
    "  b:SetItem(12, 0, gMenu:AddAttributeEditHD(0, 0, 0, 0, p, 'PC settings.Anisotropic|0.0;1.0;2.0;3.0;4.0;5.0|Original;Off;2x;4x;8x;16x', 'Anisotropic Filtering (on restart)'))\n"
    "  b:SetItem(13, 0, gMenu:AddAttributeEditHD(0, 0, 0, 0, p, 'PC settings.Upscaler|0.0;1.0|Bilinear;AMD FSR 1', 'Upscaling'))\n"
    "  b:SetItem(14, 0, gMenu:AddAttributeSliderHD(0, 0, 0, 0, 5, 0.5, p, 'PC settings.Sharpness', 'FSR Sharpness'))\n"
    "  b:SetItem(15, 0, gMenu:AddAttributeSliderHD(0, 0, 0, 0, 5, 0.5, p, 'PC settings.MouseYaw', 'Mouse Left/Right Sensitivity'))\n"
    "  b:SetItem(16, 0, gMenu:AddAttributeSliderHD(0, 0, 0, 0, 5, 0.5, p, 'PC settings.MousePitch', 'Mouse Up/Down Sensitivity'))\n"
    "  b:SetItem(17, 0, gMenu:AddAttributeEditHD(0, 0, 0, 0, p, 'PC settings.MouseInvert|true;false|$Conditions.On$;$Conditions.Off$', 'Mouse Invert Look'))\n"
    "  b:SetItem(18, 0, gMenu:AddAttributeEditHD(0, 0, 0, 0, p, 'PC settings.Speakers|0.0;1.0;2.0|Stereo;5.1;7.1', 'Audio Output (on restart)'))\n"
    "  b:SetItem(19, 0, gMenu:AddAttributeEditHD(0, 0, 0, 0, p, 'PC settings.Language|0.0;1.0;2.0;3.0;4.0|System;English (US);English (UK);French;Spanish', 'Language (on restart)'))\n"
    "  b:SetItem(20, 0, gMenu:AddAttributeEditHD(0, 0, 0, 0, p, 'PC settings.IntroMovie|true;false|$Conditions.On$;$Conditions.Off$', 'Opening Movie (on restart)'))\n"
#ifndef _WIN32   /* MangoHUD is a Linux Vulkan layer (under Proton: mangohud %command%) */
    "  b:SetItem(21, 0, gMenu:AddAttributeEditHD(0, 0, 0, 0, p, 'PC settings.Overlay|false;true|Off;MangoHUD', 'Performance Overlay (on restart)'))\n"
#endif
    " end\n"
    "end\n"
    /* Pause > Controls > Advanced Settings */
    "if ControllerConfigAdvancedEntry and not KZPC then\n"
    " KZPC = 1\n"
    " local e = ControllerConfigAdvancedEntry\n"
    " ControllerConfigAdvancedEntry = function()\n"
    "  %e()\n"
    "  local p = gGamePlayInfo:GetPlayerProfile(CURRENT_SELECTED_PLAYER)\n"
    "  local b = gControllerAdvanced.ListBox\n"
    "  b:SetItem(7, 0, gMenu:AddAttributeSlider(0, 0, 0, 0, 5, 0.5, p, 'ControllerConfig.MouseYawSensitivity', 'Mouse Left/Right Sensitivity'))\n"
    "  b:SetItem(8, 0, gMenu:AddAttributeSlider(0, 0, 0, 0, 5, 0.5, p, 'ControllerConfig.MousePitchSensitivity', 'Mouse Up/Down Sensitivity'))\n"
    "  b:SetItem(9, 0, gMenu:AddAttributeEditHD(0, 0, 0, 0, p, 'ControllerConfig.MouseInvert|true;false|$Conditions.On$;$Conditions.Off$', 'Mouse Invert Look'))\n"
    " end\n"
    " ControllerConfigAdvancedInit = function()\n"
    "  gMenu:SetAdjust(ADJUST_TO_TOP)\n"
    "  MENU:SetTextFormat(FONT_TITLE, CENTER, CENTER)\n"
    "  MENU:AddText(0, 0, 512, 80, '<line align=center>$PauseMenu.PauseTitle')\n"
    "  gMenu:SetTextFormat(gTypefaces.ItemList, LEFT, CENTER)\n"
    "  local y = B.B2.first_item_y\n"
    "  if gGameSystem:IsWideScreen() then y = y - 30 end\n"
    "  gControllerAdvanced.ListBox = gMenu:AddListBox(B.B2.item_x, y, B.B2.item_width, 10 * 30)\n"
    "  gControllerAdvanced.ListBox:SetBackgroundImagesHD('Ingame_background', 'Ingame_background_selected', 'Ingame_background', B.B2.background_x_offset, B.B2.background_y_offset)\n"
    "  gControllerAdvanced.ListBox:SetItemHeight(30)\n"
    " end\n"
    "end\n"
    /* the pause menu */
    "if gPauseMenu and BASE_CLASS and not KZPCP then\n"
    " KZPCP = 1\n"
    " gScreenPC = BASE_CLASS:CreateMenu('ScreenPC')\n"
    " gScreenPC.Init = function(self)\n"
    "  BASE_CLASS.Init(self)\n"
    "  gMenu:SetTextFormat(gTypefaces.ItemList, LEFT, CENTER)\n"
    "  self.ListBox = gMenu:AddListBox(B.B2.item_x, B.B2.first_item_y, B.B2.item_width, 30 * 10)\n"
    "  self.ListBox:SetScrollable(SCROLLING.UPDOWN)\n"
    "  self.ListBox:SetBackgroundImagesHD('Ingame_background', 'Ingame_background_selected', 'Ingame_background', B.B2.background_x_offset, B.B2.background_y_offset)\n"
    "  self.ListBox:SetItemHeight(30)\n"
    "  self.TITLE = 'PC'\n"
    " end\n"
    " gScreenPC.Entry = function(self)\n"
    "  BASE_CLASS.Entry(self)\n"
    "  self.ListBox:Clear()\n"
    "  gMenu:SetTextFormat(gTypefaces.ItemList, LEFT, CENTER)\n"
    "  KZPCROWS(self.ListBox)\n"
    " end\n"
    " gPauseMenu.InitMenuItems = function(self)\n"
    "  local b = self.ListBox\n"
    /* once per list box, whoever calls it and however often: pausing and
     * unpausing fast could make the items twice */
    "  b:Clear()\n"
    "  gGameSystem:SetAttribute('PC settings.Trace', 'True')\n"
    "  b:AddTextItem('$PauseMenu.Resume') b:SetItemCookie(0, 0)\n"
    "  b:AddTextItem('$PauseMenu.Audio') b:SetItemCookie(1, 2)\n"
    "  b:AddTextItem('$PauseMenu.Controls') b:SetItemCookie(2, 3)\n"
    "  b:AddTextItem('PC') b:SetItemCookie(3, 7)\n"
    "  b:AddTextItem('$PauseMenu.Restart') b:SetItemCookie(4, 4)\n"
    "  b:AddTextItem('$PauseMenu.Quit') b:SetItemCookie(5, 5)\n"
    "  if not gGameSystem:IsSubmission() then b:AddTextItem('Debug Options') b:SetItemCookie(6, 6) end\n"
    " end\n"
    /* made again each time the menu opens: a menu opened while the game was
     * still loading (pausing to skip the intro) also got the game's own items
     * after these, twice the list */
    " local entry = gPauseMenu.Entry\n"
    " gPauseMenu.Entry = function(self) %entry(self) if self.ListBox then self:InitMenuItems() end end\n"
    " local sub = gPauseMenu.InitSubMenus\n"
    " gPauseMenu.InitSubMenus = function(self) %sub(self) self.gScreenPC = gScreenPC end\n"
    " local sel = gPauseMenu.SelectPressed\n"
    " gPauseMenu.SelectPressed = function(self)\n"
    "  if self.ListBox:GetSelectedItemCookie() == 7 then gMenu:PlaySound(SOUND_SELECT) self.gScreenPC:Switch(TRUE)\n"
    "  else %sel(self) end\n"
    " end\n"
    " if gPauseMenu.ListBox then\n"   /* the menu was already made: make its items again */
    "  gPauseMenu.ListBox:Clear()\n"
    "  gPauseMenu:InitMenuItems()\n"
    "  gPauseMenu.gScreenPC = gScreenPC\n"
    " end\n"
    "end\n"
    /* the frontend's Options */
    "if ScreenSettingsInit and gScreenSettings and gMainMenu and not KZPCF then\n"
    " KZPCF = 1\n"
    " gScreenPC = {}\n"
    " ScreenPCInit = function()\n"
    "  gMenu:SetTextFormat(gTypefaces.ItemList, LEFT, CENTER)\n"
    "  gMenu:SetSelectionIndent(B.B2.item_indent)\n"
    "  gScreenPC.ListBox = gMenu:AddListBox(B.B2.item_x, B.B2.first_item_y, B.B2.item_width, 7 * B.B2.get_item_height())\n"
    "  gScreenPC.ListBox:SetScrollable(SCROLLING.UPDOWN)\n"
    "  gScreenPC.ListBox:SetBackgroundImagesHD(B.B2.get_background_image(), B.B2.get_background_img_selected(), B.B2.get_background_img_empty(), B.B2.background_x_offset, B.B2.background_y_offset)\n"
    "  gScreenPC.ListBox:SetItemHeight(B.B2.get_item_height())\n"
    " end\n"
    " ScreenPCEntry = function()\n"   /* rows on the profile are made on entry, like Advanced Settings' */
    "  gScreenPC.ListBox:Clear()\n"
    "  gMenu:SetTextFormat(gTypefaces.ItemList, LEFT, CENTER)\n"
    "  KZPCROWS(gScreenPC.ListBox)\n"
    "  gHeaderFooter:Show()\n"
    "  gHeaderFooter:SetTitle('PC', '')\n"
    "  gHeaderFooter:SetTitleLeftAlign(B.B2.headerfooter_title_align_x)\n"
    "  gHeaderFooter:SetControlsText('$FrontEnd.LeftRight$  $FrontEnd.UpDown$  $FrontEnd.Back$')\n"
    " end\n"
    " ScreenPCUpdate = function()\n"
    "  if gMenu:IsPressed(JOY_SELECT) then gMenu:PlaySound(SOUND_SELECT)\n"
    "  elseif gMenu:IsPressed(JOY_CANCEL) then\n"
    "   if gScreenPC.ListBox:HasChangedAttributes(0) then\n"
    "    ScreenSelectProfile.PrepareProfile()\n"
    "    SaveProfile.Begin(gGamePlayInfo:GetPlayerProfile(0), ProfileSaveSuccess, ProfileSaveFailed, TRUE)\n"
    "   end\n"
    "   gMenu:PlaySound(SOUND_BACK) gMenu:ReturnToPreviousSafeMenu()\n"
    "  end\n"
    " end\n"
    " ScreenSettingsInit = function()\n"
    "  gMenu:SetTextFormat(gTypefaces.SubMenuList, LEFT, CENTER)\n"
    "  local y = MAIN_ITEMS_Y\n"
    "  local x = gMainMenu.Settings:GetTextLeft()\n"
    "  local lang = gGameSystem:IsSCEE() or gGameSystem:IsSCEA()\n"
    "  local S = gScreenSettings\n"
    "  S.Quit = gMenu:AddAlignedText(x, y, 512, SUBITEM_VSPACING, 'Quit Game', 0)\n"
    "  y = y - SUBITEM_VSPACING\n"
    "  S.Statistics = gMenu:AddAlignedText(x, y, 512, SUBITEM_VSPACING, '$FrontEnd.ProfileDetailsStatistics', 0)\n"
    "  y = y - SUBITEM_VSPACING\n"
    "  if lang then S.Language = gMenu:AddAlignedText(x, y, 512, SUBITEM_VSPACING, '$FrontEnd.Language', 0) y = y - SUBITEM_VSPACING end\n"
    "  S.Audio = gMenu:AddAlignedText(x, y, 512, SUBITEM_VSPACING, '$FrontEnd.Audio', 0)\n"
    "  y = y - SUBITEM_VSPACING\n"
    "  S.PC = gMenu:AddAlignedText(x, y, 512, SUBITEM_VSPACING, 'PC', 0)\n"
    "  if lang then S.Language:SetHiddenRect(x, MAIN_HEADER_Y, 512, 40) end\n"
    "  S.Audio:SetHiddenRect(x, MAIN_HEADER_Y, 512, 40)\n"
    "  S.PC:SetHiddenRect(x, MAIN_HEADER_Y, 512, 40)\n"
    "  S.Quit:SetHiddenRect(x, MAIN_HEADER_Y, 512, 40)\n"
    "  S.mTextLeft = x\n"
    "  if lang then S.Language:SetBackgroundImageHD('', 'SubMenuList_C', -100, 0) end\n"
    "  S.Audio:SetBackgroundImageHD('', 'SubMenuList_C', -100, 0)\n"
    "  S.PC:SetBackgroundImageHD('', 'SubMenuList_C', -100, 0)\n"
    "  S.Statistics:SetBackgroundImageHD('', 'SubMenuList_C', -100, 0)\n"
    "  S.Quit:SetBackgroundImageHD('', 'SubMenuList_C', -100, 0)\n"
    "  S.Selection = gMenu:CreateSelection()\n"
    "  S.Selection:SetStyle(INVISIBLE, COLOR_SELECTION)\n"
    "  S.Selection:AddSubMenu(S.PC, 'ScreenPC')\n"
    "  S.Selection:AddSubMenu(S.Audio, 'ScreenAudio')\n"
    "  if lang then S.Selection:AddSubMenu(S.Language, 'ScreenLanguage') end\n"
    "  S.Selection:AddSubMenu(S.Statistics, 'ScreenProfileStats')\n"
    "  S.Selection:AddItem(S.Quit)\n"
    "  S.Selection:Select(S.PC)\n"   /* the topmost item, as in every other submenu */
    " end\n"
    " local su = ScreenSettingsUpdate\n"
    " ScreenSettingsUpdate = function()\n"   /* Quit Game: through the PC settings' Quit attribute */
    "  if gMenu:IsPressed(JOY_SELECT) and gScreenSettings.Selection:IsSelected(gScreenSettings.Quit) then\n"
    "   gMenu:PlaySound(SOUND_SELECT) gGameSystem:SetAttribute('PC settings.Quit', 'True')\n"
    "  else %su() end\n"
    " end\n"
    " gScreenSettings.UpdateTextPos = function()\n"
    "  gMainMenu.Settings:LocalizeText()\n"
    "  local x = gMainMenu.Settings:GetTextLeft() + 7\n"
    "  local S = gScreenSettings\n"
    "  if x ~= S.mTextLeft then\n"
    "   local y = MAIN_ITEMS_Y\n"
    "   if S.Quit then S.Quit:MoveTo(x, y, 0) y = y - SUBITEM_VSPACING end\n"
    "   S.Statistics:MoveTo(x, y, 0) y = y - SUBITEM_VSPACING\n"
    "   if S.Language then S.Language:MoveTo(x, y, 0) y = y - SUBITEM_VSPACING end\n"
    "   S.Audio:MoveTo(x, y, 0) y = y - SUBITEM_VSPACING\n"
    "   if S.PC then S.PC:MoveTo(x, y, 0) end\n"
    "  end\n"
    "  S.mTextLeft = x\n"
    " end\n"
    "end\n"
    /* Extras opens on its topmost visible item (Movies 1 and 2 are hidden) */
    "if ScreenExtrasInit and gScreenExtras and not KZEXT then\n"
    " KZEXT = 1\n"
    " local ei = ScreenExtrasInit\n"
    " ScreenExtrasInit = function() %ei() gScreenExtras.Selection:Select(gScreenExtras.Intro) end\n"
    "end\n";
    return L;
}

/* ---- menus with the mouse --------------------------------------------- *
 *
 * The menus pick from list boxes (the pause menu; gMenu:AddListBox, native
 * func_00275978) and selections (the frontend; below). A list box: +0x00
 * vtable 0x567340, +0x38 1.0 while shown, +0x50..+0x5C the rectangle (x0, y0,
 * x1, y1) in the menus' 512x450 layout space, +0x104 item count, +0x128 item
 * height, +0x12C the selected item. With no player tick for a moment (a menu,
 * a movie, a load) the pointer is free: the item under it is stepped to,
 * left click = X, right click = O, the wheel = up/down (or the selection's
 * own keys), and dragging with the left button held steps a slider.
 * ponytail: lists are taken unscrolled; a list longer than its box selects
 * by its visible rows as if the first item were on top. */
static uint32_t s_boxes[64];
static unsigned s_nboxes;

static void add_list_box(ppu_context* ctx)
{
    func_00275978(ctx);
    s_boxes[s_nboxes++ % 64] = (uint32_t)ctx->gpr[3];
}

/* Selections (gMenu:CreateSelection, func_0026D03C) are the frontend's
 * menus: +0x08 group count, +0x10 groups (0x10 each: item count, -, items
 * {widget, name} x8 bytes, selected item), +0x18 the current group. Their
 * items are draw objects with the same +0x38 / +0x50 fields as a list box. */
static uint32_t s_sels[128];
static unsigned s_nsels;

static void create_selection(ppu_context* ctx)
{
    func_0026D03C(ctx);
    s_sels[s_nsels++ % 128] = (uint32_t)ctx->gpr[3];
}

/* Selection:Update(prev key, next key) (func_00276DC0) runs every frame for
 * each selection the menu is driving -- in a submenu both the submenu's
 * (up/down) and the main bar's (left/right) -- and says with which keys
 * each one moves. Hover may steer any of them. */
struct LiveSel { uint32_t sel; int prev, next; long long ms; };
static LiveSel s_live[8];

static void selection_update(ppu_context* ctx)
{
    const uint32_t sel = (uint32_t)ctx->gpr[3];
    const long long t = now_ms();
    LiveSel* slot = &s_live[0];
    for (LiveSel& l : s_live) {
        if (l.sel == sel) { slot = &l; break; }
        if (l.ms < slot->ms) slot = &l;   /* else the stalest */
    }
    *slot = { sel, (int)ctx->gpr[4], (int)ctx->gpr[5], t };
    func_00276DC0(ctx);
}
static const LiveSel* live_sel(uint32_t sel)
{
    const long long t = now_ms();
    for (const LiveSel& l : s_live) if (l.sel && l.sel == sel && t - l.ms < 200) return &l;
    return nullptr;
}

static bool shown(uint32_t w) { return w && rdf(w + 0x38) == 1.0f; }
static bool inside(uint32_t w, float vx, float vy)
{
    return vx >= rdf(w + 0x50) && vx < rdf(w + 0x58) && vy >= rdf(w + 0x54) && vy < rdf(w + 0x5C);
}

/* Hover: the item under the pointer becomes the goal, and the menu is
 * stepped to it with the d-pad, one press at a time, so the game's own
 * handlers run (sounds, the tab marker, submenus). */
struct Goal { uint32_t sel, grp, box; int target, steps; bool then_x; };   /* then_x: a click, X once there */
static Goal s_goal;

/* JOY_LEFT, JOY_RIGHT, JOY_UP, JOY_DOWN (0..3) as pad buttons. */
static unsigned joy_mask(int k) { static const unsigned m[4] = { 0x0080, 0x0020, 0x0010, 0x0040 }; return k >= 0 && k < 4 ? m[k] : 0; }
/* The selection the wheel moves: one driven with up/down (a submenu) before
 * one driven with left/right (the main bar), or null. */
static const LiveSel* newest_live(void)
{
    const LiveSel* best = nullptr;
    for (const LiveSel& l : s_live) {
        if (!live_sel(l.sel)) continue;
        const bool vert = l.prev == 2, best_vert = best && best->prev == 2;
        if (!best || (vert && !best_vert) || (vert == best_vert && l.ms > best->ms)) best = &l;
    }
    return best;
}

/* The shown item of a driven selection's current group under (vx, vy). */
static bool hover_one(uint32_t sel, float vx, float vy, Goal* g);
static bool hover_selection(float vx, float vy, Goal* g)
{
    /* a submenu (driven up/down) before the main bar it opened from */
    for (int vert = 1; vert >= 0; vert--)
        for (const LiveSel& l : s_live)
            if ((l.prev == 2) == (vert == 1) && live_sel(l.sel) && hover_one(l.sel, vx, vy, g)) return true;
    return false;
}
static bool hover_one(uint32_t sel, float vx, float vy, Goal* g)
{
    const int ng = (int)rd32(sel + 0x08), cg = (int)rd32(sel + 0x18);
    if (ng <= 0 || ng > 64 || cg < 0 || cg >= ng) return false;
    const uint32_t grp = rd32(sel + 0x10) + 0x10u * cg;
    const int n = (int)rd32(grp);
    /* An item's text is narrower than the bar drawn behind it: a column of
     * items (a submenu) takes the whole band of each row across the column,
     * a row of them (the main bar) the whole height of the bar. */
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;   /* the items' extent */
    float cx0 = 1e9f, cx1 = -1e9f, cy0 = 1e9f, cy1 = -1e9f;   /* and their centres' */
    int shown_n = 0;
    for (int i = 0; i < n && i < 256; i++) {
        const uint32_t w = rd32(rd32(grp + 8) + 8u * i);
        if (!shown(w)) continue;
        shown_n++;
        const float a = rdf(w + 0x50), b = rdf(w + 0x54), c = rdf(w + 0x58), d = rdf(w + 0x5C);
        x0 = std::min(x0, a); x1 = std::max(x1, c); y0 = std::min(y0, b); y1 = std::max(y1, d);
        cx0 = std::min(cx0, (a + c) / 2); cx1 = std::max(cx1, (a + c) / 2);
        cy0 = std::min(cy0, (b + d) / 2); cy1 = std::max(cy1, (b + d) / 2);
    }
    if (!shown_n) return false;
    const bool column = (cy1 - cy0) >= (cx1 - cx0);   /* items stacked, not side by side */
    for (int i = 0; i < n && i < 256; i++) {
        const uint32_t w = rd32(rd32(grp + 8) + 8u * i);
        if (!shown(w)) continue;
        const bool hit = column
            ? vy >= rdf(w + 0x54) && vy < rdf(w + 0x5C) && vx >= x0 - 120 && vx < x1 + 120
            : vx >= rdf(w + 0x50) - 40 && vx < rdf(w + 0x58) + 40 && vy >= y0 && vy < y1 + 15;
        if (hit) { *g = { sel, grp, 0, i, 0 }; return true; }
    }
    return false;
}

struct OptRow { uint32_t w; bool slider; };
static OptRow s_opts[128];
static unsigned s_nopts;

/* List boxes that are longer than their box scroll with the selection, in
 * proportion to it (the list's update, func_00263118): the target scroll is
 * (rows * row height - box height) * selected / (rows - 1), and +0xDC eases
 * toward it. The row on screen under the pointer is the one a click means;
 * hover picks the row that the scroll then leaves under the pointer, so the
 * list settles with the highlighted row where the pointer is. -1: no row. */
static int box_row_at(uint32_t box, float vy, bool hover = false)
{
    const float y0 = rdf(box + 0x54), y1 = rdf(box + 0x5C);
    const int ih = (int)rd32(box + 0x128), n = (int)rd32(box + 0x104);
    if (vy < y0 || vy >= y1 || ih <= 0 || n <= 0) return -1;
    const float over = (float)(n * ih) - (y1 - y0);   /* how far the list scrolls in all */
    int r;
    if (over <= 0 || n < 2) r = (int)((vy - y0) / ih);
    else if (hover) r = std::min((int)((vy - y0) / (ih - over / (n - 1))), n - 1);   /* the box's foot: the last row */
    else r = (int)((vy - y0 + rdf(box + 0xDC)) / ih);
    return r >= 0 && r < n ? r : -1;
}

/* The d-pad press that moves the goal's menu one step toward it, 0 when there. */
static unsigned goal_step(const Goal& g)
{
    if (g.box) {
        const int cur = (int)rd32(g.box + 0x12C);
        return cur == g.target ? 0 : cur < g.target ? 0x0040 : 0x0010;
    }
    const int cur = (int)rd32(g.grp + 0x0C);
    const LiveSel* l = live_sel(g.sel);
    if (cur == g.target || !l) return 0;
    return joy_mask(g.target > cur ? l->next : l->prev);
}

/* gMenu:DialogBox (native func_00272670) runs its own loop until a button is
 * pressed, so menu_mouse runs on a thread of its own. The dialog is at menu
 * +0x19C: its button labels at +0x128 (count +0x12C, 0x64 each: +0x08 width,
 * +0x58 x, +0x60 the code a click gives: 200, 201...), and +0x58 the answer
 * the loop waits for (-2 until there is one). The row of buttons is at
 * y 300..355. */
static volatile uint32_t s_dialog_menu;

/* Option rows: gMenu:AddAttributeEdit(HD) (a value that cycles) and
 * AddAttributeSlider(HD), natives func_002750D8 / func_00275200 and
 * func_00275330 / func_00275460. A list places them in its rows, so their
 * own rectangle (+0x50) is the row. Click on a cycling value = right; the
 * wheel over either = left/right. */
template <void (*F)(ppu_context*), bool Slider> static void add_option(ppu_context* ctx)
{
    F(ctx);
    s_opts[s_nopts++ % 128] = { (uint32_t)ctx->gpr[3], Slider };
}
static const OptRow* option_at(float vx, float vy)
{
    for (unsigned i = 0; i < 128 && i < s_nopts; i++) {
        const OptRow& o = s_opts[(s_nopts - 1 - i) % 128];
        if (o.w && rdf(o.w + 0x38) == 1.0f && vy >= rdf(o.w + 0x54) && vy < rdf(o.w + 0x5C)) return &o;
    }
    return nullptr;
}

static void dialog_box(ppu_context* ctx)
{
    s_dialog_menu = (uint32_t)ctx->gpr[3];
    func_00272670(ctx);
    s_dialog_menu = 0;
}

static bool dialog_click(float vx, float vy)
{
    const uint32_t menu = s_dialog_menu, dlg = menu ? rd32(menu + 0x19C) : 0;
    if (!dlg || vy < 295 || vy > 355) return false;
    const uint32_t labels = rd32(dlg + 0x128);
    const int n = (int)rd32(dlg + 0x12C);
    for (int i = 0; i < n && i < 8; i++) {
        const uint32_t l = labels + 0x64u * i;
        const float x = rdf(l + 0x58), w = rdf(l + 0x08);
        if (vx >= x - 4 && vx < x + w + 28) { wr32(dlg + 0x58, rd32(l + 0x60)); return true; }
    }
    return false;
}

static void menu_mouse(void)
{
    static long long next, next_step;
    const long long t = now_ms();
    if (t < next) return;
    next = t + 8;
    const bool ui = t - s_last_play_ms > 150;
    g_pad_mouse_ui = ui;
    /* 144 Hz clock: flips go through at once in play; menus, movies and loads
     * keep the 144 Hz cadence (they are not held to the game clock, and ran
     * at ~900 fps with it). KZ_FLIP_PLAY_HZ changes the rate in play. */
    static const double play_hz = getenv("KZ_FLIP_PLAY_HZ") ? atof(getenv("KZ_FLIP_PLAY_HZ")) : 1000.0;
    if (g_kz_tick_hz >= 100) g_kz_flip_hz = ui ? 144.0 : play_hz;
    int x, y, w, h, wheel; unsigned b;
    pad_mouse_ui_state(&x, &y, &w, &h, &b, &wheel);
    static unsigned prev_b; static int prev_x = -1, prev_y = -1, drag_x;
    const unsigned pressed = b & ~prev_b;
    prev_b = b;
    if (!ui || !w || !h) { prev_x = -1; s_goal = {}; return; }
    /* The menus lay out on 512 x 450, over the 16:9 frame: in the middle of a
     * widescreen picture (hud.cpp widens their layout by the window's aspect
     * over 16:9), else the frame as shown, with bars around it. */
    float fx0 = 0.0f, fy0 = 0.0f, fw = (float)w, fh = (float)h;
    if (kz_view.aspect > 0.0f) { fw = (float)w * (16.0f / 9.0f) / kz_view.aspect; fx0 = ((float)w - fw) * 0.5f; }
    else if (w * 9 > h * 16) { fw = h * 16.0f / 9.0f; fx0 = ((float)w - fw) * 0.5f; }
    else { fh = w * 9.0f / 16.0f; fy0 = ((float)h - fh) * 0.5f; }
    const float vx = (x - fx0) * 512.0f / fw, vy = (y - fy0) * 450.0f / fh;
    /* The box shown most recently made is the one in front. */
    uint32_t box = 0;
    for (unsigned i = 0; i < 64 && i < s_nboxes; i++) {
        const uint32_t lb = s_boxes[(s_nboxes - 1 - i) % 64];
        if (lb && rd32(lb) == 0x00567340 && rdf(lb + 0x38) == 1.0f) { box = lb; break; }
    }
    static const bool fake_pos = getenv("PAD_MOUSE_POS") != nullptr;   /* tests: hover every tick */
    const bool moved = x != prev_x || y != prev_y;
    if (moved || fake_pos) {
        Goal g = {};
        if (box) {
            if (!moved) goto hovered;   /* a list re-scrolls to its selection: only a move picks a row */
            /* Rows are drawn the full width of the screen: only y counts. The
             * row under the pointer is selected at once, as a PC menu does;
             * the list draws its highlight from the selected index. */
            const int r = box_row_at(box, vy, true);
            if (r >= 0 && r != (int)rd32(box + 0x12C)) wr32(box + 0x12C, (uint32_t)r);
        } else hover_selection(vx, vy, &g);
        if ((g.box || g.grp) && (g.box != s_goal.box || g.grp != s_goal.grp || g.target != s_goal.target))
            s_goal = g;
    }
hovered:
    prev_x = x; prev_y = y;
    const OptRow* opt = nullptr;
    if ((pressed & 1) && s_dialog_menu) { dialog_click(vx, vy); s_goal = {}; }  /* a dialog's button, or nothing */
    else if ((pressed & 1) && (opt = option_at(vx, vy)) && !opt->slider) {
        /* the row first, at once: the press goes to the selected row */
        if (box) { const int r = box_row_at(box, vy); if (r >= 0) wr32(box + 0x12C, (uint32_t)r); }
        pad_inject(0x0020, 2); s_goal = {};
    }
    else if (pressed & 1) {   /* SDL_BUTTON_LMASK: X -- on an item, where there are items */
        Goal over = {};
        const bool menu = box || newest_live();
        if (!menu || (box && box_row_at(box, vy) >= 0) || (!box && hover_selection(vx, vy, &over))) {
            if (over.grp && (int)rd32(over.grp + 0x0C) != over.target) { s_goal = over; s_goal.then_x = true; }   /* go first */
            else { pad_inject(0x4000, 2); s_goal = {}; }
        }
        drag_x = x;
    }
    else if (pressed & 4)   { pad_inject(0x2000, 2); s_goal = {}; }               /* SDL_BUTTON_RMASK: O */
    else if (wheel && (opt = option_at(vx, vy))) {   /* over an option: change it */
        if (box) { const int r = box_row_at(box, vy); if (r >= 0) wr32(box + 0x12C, (uint32_t)r); }
        pad_inject(wheel > 0 ? 0x0020 : 0x0080, 2);
    }
    else if (wheel)         /* the live selection's own keys, else up/down */
        pad_inject(!box && newest_live() ? joy_mask(wheel > 0 ? newest_live()->prev : newest_live()->next)
                                            : (wheel > 0 ? 0x0010 : 0x0040), 2);
    else if (b & 1) {       /* dragging: a step every 20 virtual units */
        const float d = (x - drag_x) * 512.0f / fw;
        if (d >= 20 || d <= -20) { pad_inject(d > 0 ? 0x0020 : 0x0080, 2); drag_x = x; }
    } else if ((s_goal.box || s_goal.grp) && t >= next_step) {
        const unsigned step = goal_step(s_goal);
        if (!step && s_goal.then_x && (int)rd32(s_goal.grp + 0x0C) == s_goal.target) { pad_inject(0x4000, 2); s_goal = {}; }
        else if (!step || ++s_goal.steps > 32) s_goal = {};
        else { pad_inject(step, 2); next_step = t + 30; }
    }
}

/* Runs `src` in Lua state L, from a hook (see guest()). */
static void lua_run(ppu_context* ctx, uint32_t L, const std::string& src, const char* name)
{
    const uint32_t sp = (uint32_t)((ctx->gpr[1] - 0x4000) & ~0xFull);
    const uint32_t buf = sp + 0x400;   /* above the callee's frames */
    const size_t nl = strlen(name) + 1;
    memcpy(vm_base + buf, src.c_str(), src.size() + 1);
    memcpy(vm_base + buf + src.size() + 1, name, nl);
    guest(ctx, func_0007043C, L, buf, src.size(), buf + src.size() + 1);
}

static void lua_binding(ppu_context* ctx)
{
    static uint32_t tried[16]; static unsigned n_tried; static bool busy;
    const uint32_t L = (uint32_t)ctx->gpr[3];
    if (!busy) {
        bool seen = false;
        for (unsigned i = 0; i < n_tried; i++) if (tried[i] == L) seen = true;
        /* Each new state gets the script; after that, every 50th call, a
         * one-line probe asks whether a menu script it changes has loaded
         * and is still unchanged (the pause menu is made with the level:
         * its PC item should be there when it first opens), and only then
         * the script again. Compiling the whole script every 50th call was
         * ~4% of the game's thread in play. */
        static unsigned calls;
        if (!seen || ++calls % 50 == 0) {
            if (!seen && n_tried < 16) tried[n_tried++] = L;
            busy = true;
            kz_pc_inject_attrs(ctx);
            static const std::string probe =
                "if not KZPCROWS or (gPauseMenu and BASE_CLASS and not KZPCP)"
                " or (ControllerConfigAdvancedEntry and not KZPC)"
                " or (ScreenSettingsInit and gScreenSettings and gMainMenu and not KZPCF) then"
                " gGameSystem:SetAttribute('PC settings.Ready', 'True') end";
            s_lua_ready = false;
            if (seen) lua_run(ctx, L, probe, "=kzpcq");
            if (!seen || s_lua_ready) {
                static const std::string lua = kz_lua();
                lua_run(ctx, L, lua, "=kzpc");
                static const bool tr = getenv("KZ_LUA_TRACE") != nullptr; static unsigned runs;
                if (tr) fprintf(stderr, "[kz-lua] script %u in state %08X\n", ++runs, L);
            }
            busy = false;
        }
    }
    func_00185100(ctx);
}

/* ---- keyboard and mouse layout ---------------------------------------- *
 *
 * kz1hd_keys.ini (VFS directory) is the key map cellPad reads (PAD_KEYMAP):
 * written with this layout the first time, then the player's to edit. The
 * targets are the PS3 pad's buttons, so the game's own button layout (Pause >
 * Controls) still applies on top. */
static const char kKeysIni[] = "kz1hd_keys.ini";
static const char kKeysDefault[] =
    "# Killzone HD keyboard and mouse. TARGET = input, input\n"
    "# Targets: the PS3 pad (CROSS CIRCLE SQUARE TRIANGLE L1 R1 L2 R2 L3 R3 START SELECT\n"
    "#   UP DOWN LEFT RIGHT, LS_/RS_ UP DOWN LEFT RIGHT for the sticks).\n"
    "# Inputs: key names (E, Space, Return, Escape, Tab, Backspace, Left Ctrl, Ctrl, Shift, Alt,\n"
    "#   F1...), MouseLeft, MouseRight, MouseMiddle, Mouse4, Mouse5, WheelUp, WheelDown.\n"
    "# Delete this file to get the defaults back. The mouse turns the view (options in\n"
    "# Pause > Controls > Advanced Settings); in menus it points and clicks.\n"
    "LS_UP    = W            # move\n"
    "LS_DOWN  = S\n"
    "LS_LEFT  = A\n"
    "LS_RIGHT = D\n"
    "CROSS    = E, Space, Return   # action: climb, melee, vault (menus: select)\n"
    "TRIANGLE = Q            # cycle weapons; hold to pick up a weapon\n"
    "CIRCLE   = X, Backspace # heat vision (Luger) (menus: back)\n"
    "SQUARE   = R            # reload\n"
    "L2       = Ctrl         # crouch\n"
    "L1       = MouseMiddle  # secondary fire\n"
    "R2       = G            # throw grenade, hold to cook\n"
    "R1       = MouseLeft    # primary fire\n"
    "L3       = Shift        # sprint\n"
    "R3       = MouseRight   # zoom\n"
    "UP       = WheelUp, Up      # zoom in (sniper rifle); menus\n"
    "DOWN     = WheelDown, Down  # zoom out (sniper rifle); menus\n"
    "LEFT     = Left\n"
    "RIGHT    = Right\n"
    "SELECT   = Tab          # objectives\n"
    "START    = Escape       # pause\n"
    "RS_UP    = I            # look with keys\n"
    "RS_DOWN  = K\n"
    "RS_LEFT  = J\n"
    "RS_RIGHT = L\n";

void kz_glyphs_register(void);

void kz_pc_options_register(void)
{
    kz_glyphs_register();
    if (FILE* f = fopen(kKeysIni, "r")) fclose(f);
    else if (FILE* w = fopen(kKeysIni, "w")) { fputs(kKeysDefault, w); fclose(w); }
    setenv("PAD_KEYMAP", kKeysIni, 0);
    setenv("PS3RECOMP_ESC_QUITS", "0", 0);   /* Esc pauses; closing the window quits */
    if (getenv("PAD_MOUSE_STICK")) return;
    ini_load();
    const char* e = getenv("PAD_MOUSE_SENS");
    g_look_k   = (e ? (float)atof(e) : 1.0f) * 0.022f * 3.14159265f / 180.0f / 2.27f;
    g_look_log = getenv("KZ_LOOKLOG") != nullptr;
    g_pad_mouse_direct = 1;
    ppu_register_function(0x00291FA8, player_look);
    ppu_register_function(0x00185100, lua_binding);
    ppu_register_function(0x00272670, dialog_box);
    ppu_register_function(0x002750D8, add_option<func_002750D8, false>);
    ppu_register_function(0x00275200, add_option<func_00275200, false>);
    ppu_register_function(0x00275330, add_option<func_00275330, true>);
    ppu_register_function(0x00275460, add_option<func_00275460, true>);
    std::thread([] { for (;;) { menu_mouse(); std::this_thread::sleep_for(std::chrono::milliseconds(8)); } }).detach();
    ppu_register_function(0x00275978, add_list_box);
    ppu_register_function(0x0026D03C, create_selection);
    ppu_register_function(0x00276DC0, selection_update);
}
