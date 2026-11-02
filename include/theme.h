/* ============================================================================
 * include/theme.h  -  Visual theme palette and wallpaper renderer
 * ============================================================================ */

#ifndef NOVA_THEME_H
#define NOVA_THEME_H

#include "types.h"

typedef struct {
    u32 bg0, bg1;          /* desktop gradient                  */
    u32 bg_glow;           /* accent glow                       */
    u32 panel;             /* top bar / dock                    */
    u32 panel2;            /* raised panel                      */
    u32 panel_border;
    u32 win, win2;         /* window body / alternate           */
    u32 title;             /* title bar                         */
    u32 text;
    u32 text_dim;
    u32 text_faint;
    u32 accent;
    u32 accent2;
    u32 accent_soft;
    u32 border;
    u32 success, warn, danger;
    u32 term_bg, term_text;
    u32 selection;
} theme_t;

extern theme_t theme;

extern const u32  accent_colors[5];
extern const u32  accent_colors2[5];
extern const char *accent_names[5];
extern const char *wallpaper_names[4];

void theme_init(void);
void theme_apply(int accent, int wallpaper);
void theme_render_wallpaper(void);

#endif /* NOVA_THEME_H */
