/* ============================================================================
 * src/gui/theme.c  -  Colour palette and wallpaper renderer
 * ============================================================================ */

#include "kernel.h"
#include "gfx.h"
#include "theme.h"
#include "settings.h"

theme_t theme;

const u32 accent_colors[5] = {
    0x38BDF8,   /* cyan    */
    0xA78BFA,   /* violet  */
    0x34D399,   /* emerald */
    0xFBBF24,   /* amber   */
    0xFB7185    /* rose    */
};

const u32 accent_colors2[5] = {
    0x818CF8,
    0x6366F1,
    0x10B981,
    0xF59E0B,
    0xE11D48
};

const char *accent_names[5] = { "Cyan", "Violet", "Emerald", "Amber", "Rose" };
const char *wallpaper_names[4] = { "Aurora", "Grid", "Nebula", "Slate" };

/* ── Soft radial glow (no sqrt: squared falloff) ──────────────────────────── */

static void radial_glow(int cx, int cy, int radius, u32 color, int max_alpha)
{
    if (radius <= 0) return;
    int r2 = radius * radius;
    int x0 = max(0, cx - radius);
    int y0 = max(0, cy - radius);
    int x1 = min(gfx.width, cx + radius);
    int y1 = min(gfx.height, cy + radius);

    for (int y = y0; y < y1; y++) {
        int dy = y - cy;
        for (int x = x0; x < x1; x++) {
            int dx = x - cx;
            int d2 = dx * dx + dy * dy;
            if (d2 >= r2) continue;
            int falloff = r2 - d2;
            /* Two-step scaling keeps the quadratic falloff inside 32 bits:
             * falloff can be as large as r2 (147k when r=384). */
            int a = (max_alpha * falloff) / r2;
            a = (a * falloff) / r2;
            if (a <= 0) continue;
            u32 *p = &gfx.target[y * gfx.width + x];
            *p = gfx_blend(*p, color, (u8)min(255, a));
        }
    }
}

static u32 rng_state = 0xA5F13D21u;
static u32 rng_next(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static void starfield(int count, int max_y, u32 tint)
{
    for (int i = 0; i < count; i++) {
        int x = (int)(rng_next() % (u32)gfx.width);
        int y = (int)(rng_next() % (u32)max_y);
        int bright = 30 + (int)(rng_next() % 90);
        u8 a = (u8)bright;
        gfx_blend_pixel(x, y, 0xFFFFFF, a);
        if ((rng_next() & 7) == 0)
            gfx_blend_pixel(x + 1, y, tint, (u8)(a / 2));
    }
}

static void wallpaper_aurora(void)
{
    gfx_gradient_v(0, 0, gfx.width, gfx.height, theme.bg0, theme.bg1);

    radial_glow(gfx.width / 4, gfx.height / 3, gfx.height / 2,
                theme.accent, 140);
    radial_glow((gfx.width * 3) / 4, (gfx.height * 2) / 3, gfx.height / 2,
                theme.accent2, 110);
    radial_glow(gfx.width / 2, gfx.height + 40, gfx.height / 2,
                theme.accent, 75);

    /* Faint diagonal light beams. */
    for (int i = 0; i < 5; i++) {
        int x = (gfx.width / 6) * i - gfx.width / 6;
        for (int y = 0; y < gfx.height; y++) {
            int px = x + y / 3;
            if (px < 0 || px >= gfx.width) continue;
            gfx_blend_pixel(px, y, theme.accent, 5);
            if (px + 1 < gfx.width)
                gfx_blend_pixel(px + 1, y, theme.accent, 3);
        }
    }

    starfield(55, gfx.height * 2 / 3, theme.accent);
}

static void wallpaper_grid(void)
{
    gfx_gradient_v(0, 0, gfx.width, gfx.height, theme.bg0, theme.bg1);
    radial_glow(gfx.width / 2, gfx.height + 120, gfx.height / 2 + 100,
                theme.accent, 105);

    int step = 48;
    for (int x = 0; x < gfx.width; x += step)
        for (int y = 0; y < gfx.height; y++)
            gfx_blend_pixel(x, y, theme.accent, 12);
    for (int y = 0; y < gfx.height; y += step)
        for (int x = 0; x < gfx.width; x++)
            gfx_blend_pixel(x, y, theme.accent, 10);

    /* Brighter intersections. */
    for (int x = 0; x < gfx.width; x += step)
        for (int y = 0; y < gfx.height; y += step)
            gfx_blend_pixel(x, y, theme.accent, 70);

    /* Horizon accent line. */
    int hy = gfx.height * 3 / 4;
    for (int x = 0; x < gfx.width; x++)
        gfx_blend_pixel(x, hy, theme.accent, 26);

    starfield(24, gfx.height / 2, theme.accent);
}

static void wallpaper_nebula(void)
{
    gfx_gradient_v(0, 0, gfx.width, gfx.height, 0x05070E, 0x160B2A);

    radial_glow(gfx.width / 5, gfx.height / 4, gfx.height / 2, 0x7C3AED, 150);
    radial_glow(gfx.width * 3 / 5, gfx.height / 2, gfx.height / 2, 0x06B6D4, 110);
    radial_glow(gfx.width / 2, gfx.height + 80, gfx.height / 2, 0xEC4899, 95);
    radial_glow(gfx.width * 4 / 5, gfx.height / 5, gfx.height / 3, 0x8B5CF6, 110);

    starfield(110, gfx.height, 0xA5B4FC);
}

static void wallpaper_slate(void)
{
    gfx_gradient_v(0, 0, gfx.width, gfx.height, 0x080C15, 0x121A2B);
    radial_glow(gfx.width * 3 / 4, gfx.height / 3, gfx.height / 2,
                theme.accent, 70);

    /* Subtle horizontal scan lines. */
    for (int y = 0; y < gfx.height; y += 4)
        for (int x = 0; x < gfx.width; x += 2)
            gfx_blend_pixel(x, y, 0xFFFFFF, 3);

    starfield(30, gfx.height, theme.accent);
}

void theme_render_wallpaper(void)
{
    if (!gfx.ready) return;

    gfx_target_wall();
    gfx_reset_clip();
    rng_state = 0xA5F13D21u + (u32)g_settings.wallpaper * 977u;

    switch (g_settings.wallpaper) {
    case 1: wallpaper_grid(); break;
    case 2: wallpaper_nebula(); break;
    case 3: wallpaper_slate(); break;
    default: wallpaper_aurora(); break;
    }

    gfx_target_back();
    gfx_reset_clip();
}

void theme_init(void)
{
    theme.bg0       = 0x070B14;
    theme.bg1       = 0x0D1730;
    theme.bg_glow   = 0x1E3A8A;
    theme.panel     = 0x0A111F;
    theme.panel2    = 0x111C30;
    theme.panel_border = 0x24324B;
    theme.win       = 0x0B1424;
    theme.win2      = 0x101B30;
    theme.title     = 0x0D1728;
    theme.text      = 0xE7EEF8;
    theme.text_dim  = 0x94A3B8;
    theme.text_faint = 0x5A6B85;
    theme.accent    = accent_colors[0];
    theme.accent2   = accent_colors2[0];
    theme.accent_soft = 0x1E3A5F;
    theme.border    = 0x233149;
    theme.success   = 0x34D399;
    theme.warn      = 0xFBBF24;
    theme.danger    = 0xF87171;
    theme.term_bg   = 0x060A12;
    theme.term_text = 0xC9D6EA;
    theme.selection = 0x1E3A5F;
}

void theme_apply(int accent, int wallpaper)
{
    if (accent < 0 || accent >= 5) accent = 0;
    if (wallpaper < 0 || wallpaper >= 4) wallpaper = 0;

    theme.accent  = accent_colors[accent];
    theme.accent2 = accent_colors2[accent];
    theme.accent_soft = gfx_lerp(theme.bg1, theme.accent, 44);
    theme.bg_glow = gfx_lerp(theme.bg0, theme.accent, 30);
    theme.selection = gfx_lerp(theme.bg1, theme.accent, 56);

    g_settings.accent = accent;
    g_settings.wallpaper = wallpaper;
    theme_render_wallpaper();
}
