/* The picture's PC settings, shared by the menu (pcoptions.cpp), the camera
 * (native_ppu/camera.cpp) and the frame loop (main.cpp). */
#ifndef KZ_VIEW_H
#define KZ_VIEW_H
#ifdef __cplusplus
extern "C" {
#endif
struct KzView {
    float aspect;      /* the cameras' aspect (the window's); 0 = the game's 16:9 */
    float fov;         /* the world camera's vertical FOV in degrees; 0 = the game's 56.25 */
    float hud_scale;   /* the HUD's size, 1 = the game's */
    float hud_pos;     /* 0 = the HUD in the 16:9 middle of the screen, 1 = at the screen's edges */
};
extern struct KzView kz_view;
/* perspective projections built (native_ppu/camera.cpp): none for a while
 * means a 2D screen (menu, movie, loading), shown at 16:9 */
extern volatile unsigned kz_view_3d;
/* the menus' 2D layouts built (native_ppu/hud.cpp): a menu on screen */
extern volatile unsigned kz_view_menu;
#ifdef __cplusplus
}
#endif
#endif
