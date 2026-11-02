/* ============================================================================
 * src/kernel/splash.c  -  Premium graphical boot splash
 * ============================================================================ */

#include "kernel.h"
#include "gfx.h"
#include "splash.h"
#include "timer.h"
#include "system.h"
#include "cpu.h"

#define SPLASH_MAX_STAGES 12
#define SPLASH_ACCENT 0x38BDF8
#define SPLASH_ACCENT2 0x818CF8

static char stage_text[SPLASH_MAX_STAGES][96];
static int  stage_ok[SPLASH_MAX_STAGES];
static int  stage_count;
static int  stages_total = 1;
static int  progress;      /* 0..1000 */

static void glow(int cx, int cy, int radius, u32 color, int max_alpha)
{
    int r2 = radius * radius;
    for (int y = max(0, cy - radius); y < min(gfx.height, cy + radius); y++) {
        int dy = y - cy;
        for (int x = max(0, cx - radius); x < min(gfx.width, cx + radius); x++) {
            int dx = x - cx;
            int d2 = dx * dx + dy * dy;
            if (d2 >= r2) continue;
            int f = r2 - d2;
            int a = (max_alpha * f) / r2;
            a = (a * f) / r2;
            if (a > 0) gfx_blend_pixel(x, y, color, (u8)min(255, a));
        }
    }
}

static void diamond(int cx, int cy, int radius, u32 color, u8 alpha)
{
    for (int dy = -radius; dy <= radius; dy++) {
        int half = radius - (dy < 0 ? -dy : dy);
        gfx_fill_blend(cx - half, cy + dy, half * 2 + 1, 1, color, alpha);
    }
}

static void draw_logo(int cx, int cy)
{
    glow(cx, cy, 210, SPLASH_ACCENT, 80);
    glow(cx, cy, 140, SPLASH_ACCENT2, 70);

    diamond(cx, cy, 66, SPLASH_ACCENT, 34);
    diamond(cx, cy, 56, 0x0B1222, 255);
    diamond(cx, cy, 56, SPLASH_ACCENT, 60);
    diamond(cx, cy, 44, 0x0B1222, 255);
    diamond(cx, cy, 32, SPLASH_ACCENT, 150);
    diamond(cx, cy, 20, 0xE0F2FE, 235);
    gfx_circle_fill(cx, cy, 5, 0xFFFFFF);
}

static void draw_background(boot_info_t *bi)
{
    gfx_target_back();
    gfx_reset_clip();
    gfx_gradient_v(0, 0, gfx.width, gfx.height, 0x05070E, 0x0B1224);

    draw_logo(gfx.width / 2, gfx.height / 3);

    gfx_text_scale(gfx.width / 2 - gfx_text_width_scale("Altitude OS", 4) / 2,
                   gfx.height / 3 + 90, "Altitude OS", 0xF1F5F9, 4);
    const char *sub = "PROGRAMMER OPERATING SYSTEM";
    gfx_text(gfx.width / 2 - gfx_text_width(sub) / 2,
             gfx.height / 3 + 140, sub, 0x7DD3FC);
    char ver[64];
    ksnprintf(ver, sizeof(ver), "version %s  |  %s", NOVA_VERSION, NOVA_CODENAME);
    gfx_text(gfx.width / 2 - gfx_text_width(ver) / 2,
             gfx.height / 3 + 162, ver, 0x64748B);

    /* System facts */
    char brand[64];
    cpuid_format_brand(brand, sizeof(brand));
    if (kstrlen(brand) > 46) brand[46] = '\0';

    int info_y = gfx.height - 96;
    gfx_text(gfx.width / 2 - 260, info_y, brand, 0x94A3B8);
    if (bi) {
        gfx_textf(gfx.width / 2 - 260, info_y + 18, 0x64748B,
                  "%u MiB RAM   |   %ux%u %ubpp   |   FB 0x%08x",
                  system_total_ram_kb() / 1024, bi->width, bi->height,
                  bi->bpp, bi->fb_addr);
    }
    gfx_text(gfx.width / 2 - 260, info_y + 36,
             "i386 protected mode  |  IDT + PIC + PIT  |  VFS  |  scheduler",
             0x475569);
}

static void draw_progress(void)
{
    int w = min(560, gfx.width - 120);
    int x = (gfx.width - w) / 2;
    int y = gfx.height * 72 / 100;

    gfx_round_rect(x, y, w, 8, 4, 0x1E293B);
    int fill = (w * progress) / 1000;
    if (fill > 0)
        gfx_round_rect(x, y, fill, 8, 4, SPLASH_ACCENT);
    if (fill > 4)
        gfx_fill(x + fill - 4, y, 4, 8, 0xE0F2FE);

    char pct[8];
    ksnprintf(pct, sizeof(pct), "%d%%", progress / 10);
    gfx_text_right(x + w + 4, y - 4, pct, 0x7DD3FC);

    int ly = y + 30;
    int base = stage_count > 8 ? stage_count - 8 : 0;
    for (int i = base; i < stage_count; i++) {
        int ry = ly + (i - base) * 18;
        u32 color = stage_ok[i] ? 0x94A3B8 : 0x475569;
        gfx_text(x, ry, stage_ok[i] ? "[ ok ]" : "[ .. ]",
                 stage_ok[i] ? 0x34D399 : 0x64748B);
        gfx_text(x + 56, ry, stage_text[i], color);
    }
}

static void draw_all(boot_info_t *bi)
{
    draw_background(bi);
    draw_progress();
}

void splash_begin(boot_info_t *bi)
{
    stage_count = 0;
    progress = 0;
    stages_total = 1;
    draw_all(bi);
    gfx_present();
}

void splash_stage(int index, int total, const char *text, int ok)
{
    if (!gfx.ready) return;
    stages_total = max(1, total);

    if (stage_count < SPLASH_MAX_STAGES) {
        kstrncpy(stage_text[stage_count], text, sizeof(stage_text[0]));
        stage_ok[stage_count] = ok;
        stage_count++;
    }

    progress = ((index + 1) * 1000) / stages_total;
    if (progress > 1000) progress = 1000;

    draw_all(g_boot_info);
    gfx_present();
    timer_sleep_ms(90);
}

void splash_finish(void)
{
    if (!gfx.ready) return;
    progress = 1000;
    if (stage_count < SPLASH_MAX_STAGES) {
        kstrncpy(stage_text[stage_count], "Handing off to the desktop compositor", sizeof(stage_text[0]));
        stage_ok[stage_count] = 1;
        stage_count++;
    }
    draw_all(g_boot_info);
    gfx_present();
    timer_sleep_ms(240);
}
