/*
 * The session header (sys_info.c): one "[kz-sys] <key>: <value>" line per fact, right after the version line, so a
 * log or a support package says what the game ran on (docs/dev/crash-reports.md). The GPU and driver lines come
 * from the Vulkan backend.
 */
#ifndef KZ_SYS_INFO_H
#define KZ_SYS_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

/* os, cpu, cores, memory, env (the KZ_* and PS3* variables set, before kz1hd_pc.ini adds its own), started-by. */
void kz_sys_log(void);

/* One more line: "[kz-sys] <key>: <value>" (settings, dlss). */
void kz_sys_log_line(const char* key, const char* value);

/* display lines, one per monitor ("WxH@Hz scale N%"), from SDL once the window exists (the frame clock's thread,
 * after the backend's init). Nothing when SDL's video is not running (headless). */
void kz_sys_log_displays(void);

/* An SDL2 function by name, looked up at run time (NULL when SDL2 is not loaded). */
void* kz_sdl_fn(const char* name);

/* Appends " name=value" (no space before the first) to out: a value with a space or a quote is quoted, the home
 * folder at the start of a value is written ~. */
void kz_sys_pair(char* out, unsigned long cap, const char* name, const char* value);

/* The env line's text for the variables in envp (NULL-terminated "NAME=value" strings). */
void kz_sys_env_line(char* const* envp, char* out, unsigned long cap);

#ifdef __cplusplus
}
#endif
#endif
