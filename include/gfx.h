/* ============================================================================
 * include/gfx.h  -  32-bit framebuffer graphics library
 * ============================================================================ */

#ifndef NOVA_GFX_H
#define NOVA_GFX_H

#include "types.h"
#include "bootinfo.h"

typedef struct {
    u32 *lfb;          /* hardware linear framebuffer                       */
    u32 *back;         /* compositor back-buffer (always 32bpp)             */
    u32 *wall;         /* cached wallpaper buffer (always 32bpp)            */
    int  width;
    int  height;
    int  pitch;        /* LFB pitch in bytes                                */
    int  bpp;          /* 24 or 32                                          */
    int  ready;
    int  clip_x, clip_y, clip_w, clip_h;
    u32 *target;       /* current drawing target (back or wall)             */
} gfx_state_t;

extern gfx_state_t gfx;

#define RGB(r, g, b) ((u32)(((r) << 16) | ((g) << 8) | (b)))
#define RGB_R(c) (((c) >> 16) & 0xFF)
#define RGB_G(c) (((c) >> 8) & 0xFF)
#define RGB_B(c) ((c) & 0xFF)

int  gfx_init(boot_info_t *bi);
void gfx_shutdown(void);

void gfx_target_back(void);
void gfx_target_wall(void);
void gfx_set_clip(int x, int y, int w, int h);
void gfx_reset_clip(void);
int  gfx_in_clip(int x, int y);

void gfx_pixel(int x, int y, u32 color);
void gfx_blend_pixel(int x, int y, u32 color, u8 alpha);
void gfx_fill(int x, int y, int w, int h, u32 color);
void gfx_fill_blend(int x, int y, int w, int h, u32 color, u8 alpha);
void gfx_clear(u32 color);
void gfx_gradient_v(int x, int y, int w, int h, u32 top, u32 bottom);
void gfx_gradient_h(int x, int y, int w, int h, u32 left, u32 right);
void gfx_line(int x0, int y0, int x1, int y1, u32 color);
void gfx_line_blend(int x0, int y0, int x1, int y1, u32 color, u8 alpha);
void gfx_circle(int cx, int cy, int radius, u32 color);
void gfx_circle_fill(int cx, int cy, int radius, u32 color);
void gfx_circle_ring(int cx, int cy, int radius, int thickness, u32 color);
void gfx_round_rect(int x, int y, int w, int h, int radius, u32 color);
void gfx_round_rect_blend(int x, int y, int w, int h, int radius, u32 color, u8 alpha);
void gfx_round_outline(int x, int y, int w, int h, int radius, u32 color);
void gfx_shadow(int x, int y, int w, int h, int radius, int spread, u8 alpha);
void gfx_blit_wall_to_back(void);
void gfx_present(void);

int  gfx_text_width(const char *s);
int  gfx_text_width_scale(const char *s, int scale);
void gfx_text(int x, int y, const char *s, u32 color);
void gfx_text_scale(int x, int y, const char *s, u32 color, int scale);
void gfx_text_alpha(int x, int y, const char *s, u32 color, u8 alpha);
void gfx_text_center(int cx, int y, const char *s, u32 color);
void gfx_text_right(int right, int y, const char *s, u32 color);
void gfx_textf(int x, int y, u32 color, const char *fmt, ...);
void gfx_textf_scale(int x, int y, u32 color, int scale, const char *fmt, ...);

u32  gfx_blend(u32 dst, u32 src, u8 alpha);
u32  gfx_lerp(u32 a, u32 b, int t);   /* t = 0..255 */

#endif /* NOVA_GFX_H */
