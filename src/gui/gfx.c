/* ============================================================================
 * src/gui/gfx.c  -  32-bit compositor graphics library
 *
 * All drawing happens into a 32-bit back-buffer.  The wallpaper is rendered
 * once into a second buffer and copied into the back-buffer before each frame.
 * gfx_present() converts to the hardware's 24/32bpp linear framebuffer.
 * ============================================================================ */

#include "kernel.h"
#include "gfx.h"
#include "font8x16.h"

gfx_state_t gfx;

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static u32 isqrt32(u32 n)
{
    u32 x = n;
    u32 y = (x + 1) >> 1;
    while (y < x) {
        x = y;
        y = (x + n / x) >> 1;
    }
    return x;
}

u32 gfx_blend(u32 dst, u32 src, u8 alpha)
{
    if (alpha == 0) return dst;
    if (alpha == 255) return src;

    u32 a = alpha;
    u32 ia = 255u - a;
    u32 rb = (((dst & 0x00FF00FFu) * ia) + ((src & 0x00FF00FFu) * a)) >> 8;
    u32 g  = (((dst & 0x0000FF00u) * ia) + ((src & 0x0000FF00u) * a)) >> 8;
    rb &= 0x00FF00FFu;
    g  &= 0x0000FF00u;
    return rb | g;
}

u32 gfx_lerp(u32 a, u32 b, int t)
{
    if (t <= 0) return a;
    if (t >= 255) return b;
    int ia = 255 - t;
    int r = (((a >> 16) & 0xFF) * ia + ((b >> 16) & 0xFF) * t) / 255;
    int g = (((a >> 8) & 0xFF) * ia + ((b >> 8) & 0xFF) * t) / 255;
    int bl = ((a & 0xFF) * ia + (b & 0xFF) * t) / 255;
    return RGB(r, g, bl);
}

/* ── Initialisation ───────────────────────────────────────────────────────── */

int gfx_init(boot_info_t *bi)
{
    if (!bi || bi->magic != BOOTINFO_MAGIC) return -1;
    if (bi->bpp != 32 && bi->bpp != 24) return -2;
    if (bi->width < 320 || bi->height < 200) return -3;
    if (bi->width > MAX_SCREEN_W || bi->height > MAX_SCREEN_H) return -4;
    if (bi->pitch < bi->width * (bi->bpp / 8)) return -5;

    gfx.lfb    = (u32 *)bi->fb_addr;
    gfx.wall   = (u32 *)WALLPAPER_ADDR;
    gfx.back   = (u32 *)BACKBUFFER_ADDR;
    gfx.width  = (int)bi->width;
    gfx.height = (int)bi->height;
    gfx.pitch  = (int)bi->pitch;
    gfx.bpp    = (int)bi->bpp;
    gfx.target = gfx.back;
    gfx.ready  = 1;

    gfx.clip_x = 0;
    gfx.clip_y = 0;
    gfx.clip_w = gfx.width;
    gfx.clip_h = gfx.height;

    gfx_fill(0, 0, gfx.width, gfx.height, 0x000000);
    return 0;
}

void gfx_shutdown(void) { gfx.ready = 0; }

void gfx_target_back(void) { gfx.target = gfx.back; }
void gfx_target_wall(void) { gfx.target = gfx.wall; }

void gfx_set_clip(int x, int y, int w, int h)
{
    int x1 = max(0, x);
    int y1 = max(0, y);
    int x2 = min(gfx.width, x + w);
    int y2 = min(gfx.height, y + h);
    gfx.clip_x = x1;
    gfx.clip_y = y1;
    gfx.clip_w = max(0, x2 - x1);
    gfx.clip_h = max(0, y2 - y1);
}

void gfx_reset_clip(void)
{
    gfx.clip_x = 0;
    gfx.clip_y = 0;
    gfx.clip_w = gfx.width;
    gfx.clip_h = gfx.height;
}

int gfx_in_clip(int x, int y)
{
    return x >= gfx.clip_x && y >= gfx.clip_y &&
           x < gfx.clip_x + gfx.clip_w && y < gfx.clip_y + gfx.clip_h;
}

/* ── Pixels and rectangles ────────────────────────────────────────────────── */

void gfx_pixel(int x, int y, u32 color)
{
    if (!gfx_in_clip(x, y)) return;
    gfx.target[y * gfx.width + x] = color;
}

void gfx_blend_pixel(int x, int y, u32 color, u8 alpha)
{
    if (!gfx_in_clip(x, y)) return;
    u32 *p = &gfx.target[y * gfx.width + x];
    *p = gfx_blend(*p, color, alpha);
}

void gfx_fill(int x, int y, int w, int h, u32 color)
{
    if (w <= 0 || h <= 0) return;

    int x1 = max(x, gfx.clip_x);
    int y1 = max(y, gfx.clip_y);
    int x2 = min(x + w, gfx.clip_x + gfx.clip_w);
    int y2 = min(y + h, gfx.clip_y + gfx.clip_h);
    if (x1 >= x2 || y1 >= y2) return;

    int count = x2 - x1;
    for (int row = y1; row < y2; row++) {
        u32 *dst = &gfx.target[row * gfx.width + x1];
        kfast_fill32(dst, color, (u32)count);
    }
}

void gfx_fill_blend(int x, int y, int w, int h, u32 color, u8 alpha)
{
    if (alpha == 0 || w <= 0 || h <= 0) return;
    if (alpha == 255) { gfx_fill(x, y, w, h, color); return; }

    int x1 = max(x, gfx.clip_x);
    int y1 = max(y, gfx.clip_y);
    int x2 = min(x + w, gfx.clip_x + gfx.clip_w);
    int y2 = min(y + h, gfx.clip_y + gfx.clip_h);
    if (x1 >= x2 || y1 >= y2) return;

    for (int row = y1; row < y2; row++) {
        u32 *dst = &gfx.target[row * gfx.width + x1];
        for (int col = 0; col < x2 - x1; col++)
            dst[col] = gfx_blend(dst[col], color, alpha);
    }
}

void gfx_clear(u32 color)
{
    gfx_reset_clip();
    gfx_fill(0, 0, gfx.width, gfx.height, color);
}

void gfx_gradient_v(int x, int y, int w, int h, u32 top, u32 bottom)
{
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++) {
        u32 c = gfx_lerp(top, bottom, (row * 255) / max(1, h - 1));
        gfx_fill(x, y + row, w, 1, c);
    }
}

void gfx_gradient_h(int x, int y, int w, int h, u32 left, u32 right)
{
    if (w <= 0 || h <= 0) return;
    for (int col = 0; col < w; col++) {
        u32 c = gfx_lerp(left, right, (col * 255) / max(1, w - 1));
        for (int row = 0; row < h; row++)
            gfx_pixel(x + col, y + row, c);
    }
}

/* ── Lines and circles ────────────────────────────────────────────────────── */

void gfx_line(int x0, int y0, int x1, int y1, u32 color)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        gfx_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_line_blend(int x0, int y0, int x1, int y1, u32 color, u8 alpha)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        gfx_blend_pixel(x0, y0, color, alpha);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_circle_fill(int cx, int cy, int radius, u32 color)
{
    if (radius <= 0) return;
    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; dy++) {
        int rem = r2 - dy * dy;
        if (rem < 0) continue;
        int dx = (int)isqrt32((u32)rem);
        gfx_fill(cx - dx, cy + dy, dx * 2 + 1, 1, color);
    }
}

void gfx_circle_ring(int cx, int cy, int radius, int thickness, u32 color)
{
    if (radius <= 0 || thickness <= 0) return;
    int outer2 = radius * radius;
    int inner = radius - thickness;
    int inner2 = inner > 0 ? inner * inner : 0;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= outer2 && d2 >= inner2)
                gfx_pixel(cx + dx, cy + dy, color);
        }
    }
}

void gfx_circle(int cx, int cy, int radius, u32 color)
{
    gfx_circle_ring(cx, cy, radius, 1, color);
}

/* ── Rounded rectangles ───────────────────────────────────────────────────── */

static void round_rect_impl(int x, int y, int w, int h, int radius,
                            u32 color, u8 alpha)
{
    if (w <= 0 || h <= 0) return;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    if (radius < 0) radius = 0;

    if (alpha == 255) {
        gfx_fill(x + radius, y, w - radius * 2, h, color);
        if (radius > 0) {
            gfx_fill(x, y + radius, radius, h - radius * 2, color);
            gfx_fill(x + w - radius, y + radius, radius, h - radius * 2, color);
        }
    } else {
        gfx_fill_blend(x + radius, y, w - radius * 2, h, color, alpha);
        if (radius > 0) {
            gfx_fill_blend(x, y + radius, radius, h - radius * 2, color, alpha);
            gfx_fill_blend(x + w - radius, y + radius, radius, h - radius * 2, color, alpha);
        }
    }

    if (radius <= 0) return;

    int full2 = radius * radius;
    int solid2 = (radius - 1) * (radius - 1);
    for (int dy = 0; dy < radius; dy++) {
        for (int dx = 0; dx < radius; dx++) {
            int d2 = (radius - dx) * (radius - dx) + (radius - dy) * (radius - dy);
            if (d2 > full2) continue;
            u8 a = alpha;
            if (d2 > solid2) a = (u8)((alpha * 140) / 255);
            int left  = x + dx;
            int right = x + w - 1 - dx;
            int top   = y + dy;
            int bot   = y + h - 1 - dy;
            if (a == 255) {
                gfx_pixel(left, top, color);
                gfx_pixel(right, top, color);
                gfx_pixel(left, bot, color);
                gfx_pixel(right, bot, color);
            } else {
                gfx_blend_pixel(left, top, color, a);
                gfx_blend_pixel(right, top, color, a);
                gfx_blend_pixel(left, bot, color, a);
                gfx_blend_pixel(right, bot, color, a);
            }
        }
    }
}

void gfx_round_rect(int x, int y, int w, int h, int radius, u32 color)
{
    round_rect_impl(x, y, w, h, radius, color, 255);
}

void gfx_round_rect_blend(int x, int y, int w, int h, int radius, u32 color, u8 alpha)
{
    round_rect_impl(x, y, w, h, radius, color, alpha);
}

void gfx_round_outline(int x, int y, int w, int h, int radius, u32 color)
{
    if (w <= 0 || h <= 0) return;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    if (radius < 0) radius = 0;

    gfx_line(x + radius, y, x + w - radius - 1, y, color);
    gfx_line(x + radius, y + h - 1, x + w - radius - 1, y + h - 1, color);
    gfx_line(x, y + radius, x, y + h - radius - 1, color);
    gfx_line(x + w - 1, y + radius, x + w - 1, y + h - radius - 1, color);

    if (radius <= 0) return;

    int outer2 = radius * radius;
    int inner = radius - 1;
    int inner2 = inner > 0 ? inner * inner : 0;
    for (int dy = 0; dy < radius; dy++) {
        for (int dx = 0; dx < radius; dx++) {
            int d2 = (radius - dx) * (radius - dx) + (radius - dy) * (radius - dy);
            if (d2 > outer2 || d2 < inner2) continue;
            gfx_pixel(x + dx, y + dy, color);
            gfx_pixel(x + w - 1 - dx, y + dy, color);
            gfx_pixel(x + dx, y + h - 1 - dy, color);
            gfx_pixel(x + w - 1 - dx, y + h - 1 - dy, color);
        }
    }
}

void gfx_shadow(int x, int y, int w, int h, int radius, int spread, u8 alpha)
{
    /* Draw concentric rings as thin strips instead of filling the entire
     * rectangle on every pass.  This keeps the cost proportional to the
     * perimeter, which matters when several windows are composited each
     * frame in software. */
    if (spread <= 0 || alpha == 0) return;

    for (int i = spread; i > 0; i -= 2) {
        u8 a = (u8)((u32)alpha * (u32)(spread - i + 2) / (u32)(spread + 2));
        if (a == 0) continue;

        int top_y = y - i + 4;
        int bot_y = y + h + i - 2;
        int left_x = x - i;
        int right_x = x + w + i - 2;

        int side_y = y + radius + 4;
        int side_h = h - radius * 2;
        if (side_h < 1) side_h = 1;

        gfx_fill_blend(x - i + radius, top_y, w + i * 2 - radius * 2, 2, 0, a);
        gfx_fill_blend(x - i + radius, bot_y, w + i * 2 - radius * 2, 2, 0, a);
        gfx_fill_blend(left_x, side_y, 2, side_h, 0, a);
        gfx_fill_blend(right_x, side_y, 2, side_h, 0, a);

        /* Fill the rounded corner gaps with a tiny soft square. */
        gfx_fill_blend(left_x, top_y, 2, 2, 0, a);
        gfx_fill_blend(right_x, top_y, 2, 2, 0, a);
        gfx_fill_blend(left_x, bot_y, 2, 2, 0, a);
        gfx_fill_blend(right_x, bot_y, 2, 2, 0, a);
    }
}

/* ── Buffer operations ────────────────────────────────────────────────────── */

void gfx_blit_wall_to_back(void)
{
    if (!gfx.ready) return;
    kfast_copy32(gfx.back, gfx.wall, (u32)(gfx.width * gfx.height));
}

void gfx_present(void)
{
    if (!gfx.ready) return;

    if (gfx.bpp == 32) {
        int pitch_dw = gfx.pitch / 4;
        for (int y = 0; y < gfx.height; y++) {
            kfast_copy32(&gfx.lfb[y * pitch_dw],
                         &gfx.back[y * gfx.width],
                         (u32)gfx.width);
        }
    } else {
        u8 *lfb = (u8 *)gfx.lfb;
        for (int y = 0; y < gfx.height; y++) {
            u8 *dst = lfb + y * gfx.pitch;
            u32 *src = &gfx.back[y * gfx.width];
            for (int x = 0; x < gfx.width; x++) {
                u32 c = src[x];
                dst[x * 3 + 0] = (u8)(c & 0xFF);
                dst[x * 3 + 1] = (u8)((c >> 8) & 0xFF);
                dst[x * 3 + 2] = (u8)((c >> 16) & 0xFF);
            }
        }
    }
}

/* ── Text ─────────────────────────────────────────────────────────────────── */

int gfx_text_width(const char *s) { return kstrlen(s) * FONT_GLYPH_W; }
int gfx_text_width_scale(const char *s, int scale)
{
    return kstrlen(s) * FONT_GLYPH_W * (scale < 1 ? 1 : scale);
}

static void text_impl(int x, int y, const char *s, u32 color, int scale, u8 alpha)
{
    if (!s) return;
    if (scale < 1) scale = 1;

    int cx = x;
    while (*s) {
        u8 c = (u8)*s++;
        if (c == '\n') {
            x = cx;
            y += FONT_GLYPH_H * scale;
            continue;
        }
        if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR) c = '?';
        const unsigned char *glyph = font8x16[c - FONT_FIRST_CHAR];

        for (int row = 0; row < FONT_GLYPH_H; row++) {
            u8 bits = glyph[row];
            int col = 0;
            while (col < FONT_GLYPH_W) {
                if (bits & (0x80 >> col)) {
                    int run = 1;
                    while (col + run < FONT_GLYPH_W &&
                           (bits & (0x80 >> (col + run))))
                        run++;
                    if (alpha == 255)
                        gfx_fill(x + col * scale, y + row * scale,
                                 run * scale, scale, color);
                    else
                        gfx_fill_blend(x + col * scale, y + row * scale,
                                       run * scale, scale, color, alpha);
                    col += run;
                } else {
                    col++;
                }
            }
        }
        x += FONT_GLYPH_W * scale;
    }
}

void gfx_text(int x, int y, const char *s, u32 color)
{
    text_impl(x, y, s, color, 1, 255);
}

void gfx_text_scale(int x, int y, const char *s, u32 color, int scale)
{
    text_impl(x, y, s, color, scale, 255);
}

void gfx_text_alpha(int x, int y, const char *s, u32 color, u8 alpha)
{
    text_impl(x, y, s, color, 1, alpha);
}

void gfx_text_center(int cx, int y, const char *s, u32 color)
{
    gfx_text(cx - gfx_text_width(s) / 2, y, s, color);
}

void gfx_text_right(int right, int y, const char *s, u32 color)
{
    gfx_text(right - gfx_text_width(s), y, s, color);
}

void gfx_textf(int x, int y, u32 color, const char *fmt, ...)
{
    char buf[256];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    gfx_text(x, y, buf, color);
}

void gfx_textf_scale(int x, int y, u32 color, int scale, const char *fmt, ...)
{
    char buf[256];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    gfx_text_scale(x, y, buf, color, scale);
}
