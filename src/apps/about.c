/* ============================================================================
 * src/apps/about.c  -  About Altitude OS
 * ============================================================================ */

#include "kernel.h"
#include "theme.h"
#include "gfx.h"
#include "widgets.h"
#include "mouse.h"
#include "timer.h"
#include "memory.h"
#include "process.h"
#include "vfs.h"
#include "system.h"
#include "cpu.h"
#include "apps.h"
#include "apps_internal.h"

#define ABOUT_HERO_H 116

typedef struct {
    int dummy;
} about_state_t;

/* â”€â”€ Drawing helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void about_logo(int cx, int cy, int r)
{
    /* A compact version of the boot-splash diamond. */
    for (int dy = -r; dy <= r; dy++) {
        int half = r - (dy < 0 ? -dy : dy);
        gfx_fill_blend(cx - half, cy + dy, half * 2 + 1, 1, theme.accent, 44);
    }
    for (int dy = -r + 8; dy <= r - 8; dy++) {
        int half = (r - 8) - (dy < 0 ? -dy : dy);
        gfx_fill(cx - half, cy + dy, half * 2 + 1, 1, theme.bg0);
    }
    for (int dy = -r + 10; dy <= r - 10; dy++) {
        int half = (r - 10) - (dy < 0 ? -dy : dy);
        gfx_fill_blend(cx - half, cy + dy, half * 2 + 1, 1, theme.accent, 150);
    }
    for (int dy = -r + 22; dy <= r - 22; dy++) {
        int half = (r - 22) - (dy < 0 ? -dy : dy);
        gfx_fill_blend(cx - half, cy + dy, half * 2 + 1, 1, theme.accent2, 210);
    }
    gfx_circle_fill(cx, cy, 5, 0xFFFFFF);
}

static void about_feature(int x, int y, int w, int h, const char *title,
                          const char *line, u32 color)
{
    gfx_round_rect(x, y, w, h, 10, theme.panel);
    gfx_round_outline(x, y, w, h, 10, theme.border);
    gfx_fill(x + 13, y + 13, 4, 4, color);
    gfx_text(x + 24, y + 8, title, theme.text);
    gfx_set_clip(x + 12, y + 28, w - 24, h - 34);
    gfx_text(x + 13, y + 30, line, theme.text_faint);
    gfx_reset_clip();
}

static void about_fact(int x, int y, const char *key, const char *value, u32 color)
{
    gfx_text(x, y, key, theme.text_faint);
    gfx_text(x + 128, y, value, color);
}

static void about_button(int x, int y, int w, int h, const char *label,
                         app_id_t app, int hot, int primary)
{
    if (app >= 0) app_icon_draw(app, x + 7, y + 6, h - 12, 0);
    int tx = x + (app >= 0 ? 30 : 0);
    int tw = w - (app >= 0 ? 30 : 0);
    u32 base = primary ? theme.accent : theme.panel2;
    if (hot) base = gfx_lerp(base, 0xFFFFFF, primary ? 30 : 20);
    gfx_round_rect(x, y, w, h, 8, base);
    gfx_round_outline(x, y, w, h, 8, primary ? theme.accent : theme.border);
    gfx_text(tx + (tw - gfx_text_width(label)) / 2, y + (h - 16) / 2, label,
             primary ? 0x06101F : theme.text);
}

/* â”€â”€ Main drawing â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void about_draw(window_t *w)
{
    int x = CX(w), y = CY(w), cw = CWD(w), ch = CHT(w);
    gfx_fill(x, y, cw, ch, theme.win);

    mouse_state_t *m = mouse_get();
    int mx = m ? m->x : -1, my = m ? m->y : -1;

    /* Hero. */
    int hero_y = y + 18;
    gfx_round_rect(x + 14, hero_y, cw - 28, ABOUT_HERO_H, 14, theme.panel);
    gfx_round_outline(x + 14, hero_y, cw - 28, ABOUT_HERO_H, 14, theme.border);
    gfx_fill_blend(x + 15, hero_y + 1, cw - 30, ABOUT_HERO_H - 2, theme.accent, 10);
    about_logo(x + 78, hero_y + ABOUT_HERO_H / 2, 38);

    gfx_text_scale(x + 130, hero_y + 20, "Altitude OS", theme.text, 3);
    gfx_textf(x + 134, hero_y + 62, theme.accent,
              "version %s  |  %s  |  build %s", NOVA_VERSION, NOVA_CODENAME, NOVA_BUILD);
    gfx_text(x + 134, hero_y + 84,
             "A compact programmer-centric operating system for 32-bit x86.",
             theme.text_dim);

    int bx = x + cw - 28 - 260;
    int by = hero_y + 26;
    about_button(bx, by, 126, 30, "Developer Guide", APP_HELP,
                 app_hit(mx, my, bx, by, 126, 30), 0);
    about_button(bx + 134, by, 126, 30, "Terminal", APP_TERMINAL,
                 app_hit(mx, my, bx + 134, by, 126, 30), 1);
    by += 38;
    about_button(bx, by, 126, 30, "System Monitor", APP_MONITOR,
                 app_hit(mx, my, bx, by, 126, 30), 0);
    about_button(bx + 134, by, 126, 30, "DevTools", APP_DEVTOOLS,
                 app_hit(mx, my, bx + 134, by, 126, 30), 0);

    /* Feature strip. */
    int fy = hero_y + ABOUT_HERO_H + 14;
    int gap = 10;
    int fw = (cw - 28 - gap * 3) / 4;
    about_feature(x + 14, fy, fw, 98, "32-bit kernel",
                  "protected mode, IDT, PIC, PIT and a flat memory model", theme.accent);
    about_feature(x + 14 + (fw + gap), fy, fw, 98, "4 schedulers",
                  "FCFS, round-robin, priority and shortest-job-first", theme.success);
    about_feature(x + 14 + (fw + gap) * 2, fy, fw, 98, "RAM filesystem",
                  "hierarchical VFS with a live /proc and script support", theme.warn);
    about_feature(x + 14 + (fw + gap) * 3, fy, fw, 98, "Live DevTools",
                  "CPUID, heap blocks, IDT gates, PCI and syscall table", theme.accent2);

    /* Facts and credits. */
    int bottom_y = fy + 98 + 14;
    int bottom_h = ch - APP_STATUS_H - 12 - (bottom_y - y);
    if (bottom_h < 120) bottom_h = 120;
    int left_w = (cw - 28 - gap) * 58 / 100;
    int right_x = x + 14 + left_w + gap;
    int right_w = cw - 28 - left_w - gap;

    gfx_round_rect(x + 14, bottom_y, left_w, bottom_h, 12, theme.panel);
    gfx_round_outline(x + 14, bottom_y, left_w, bottom_h, 12, theme.border);
    gfx_fill(x + 26, bottom_y + 12, 3, 12, theme.accent);
    gfx_text(x + 36, bottom_y + 9, "SYSTEM FACTS", theme.text);

    cpu_info_t cpu;
    cpuid_info(&cpu);
    char brand[64];
    kstrncpy(brand, cpu.brand, sizeof(brand));
    brand[sizeof(brand) - 1] = '\0';

    mem_info_t mi;
    mem_info(&mi);

    int row = bottom_y + 40;
    gfx_set_clip(x + 18, bottom_y + 32, left_w - 32, bottom_h - 38);
    about_fact(x + 30, row, "Kernel", "NOVA " NOVA_VERSION " (" NOVA_CODENAME ")", theme.text);
    about_fact(x + 30, row + 22, "Architecture", system_arch(), theme.text_dim);
    gfx_set_clip(x + 30, row + 44, left_w - 60, 16);
    about_fact(x + 30, row + 44, "Processor", brand[0] ? brand : "Generic x86", theme.text_dim);
    gfx_reset_clip();
    gfx_set_clip(x + 18, bottom_y + 32, left_w - 32, bottom_h - 38);
    char mem[64];
    ksnprintf(mem, sizeof(mem), "%u MiB detected  |  %u KiB heap",
              system_total_ram_kb() / 1024, mi.total / 1024);
    about_fact(x + 30, row + 66, "Memory", mem, theme.text_dim);
    char disp[64];
    ksnprintf(disp, sizeof(disp), "%ux%u @ %ubpp VBE", gfx.width, gfx.height, gfx.bpp);
    about_fact(x + 30, row + 88, "Display", disp, theme.text_dim);
    about_fact(x + 30, row + 110, "Uptime", "see status bar", theme.text_faint);
    gfx_reset_clip();

    gfx_round_rect(right_x, bottom_y, right_w, bottom_h, 12, theme.panel);
    gfx_round_outline(right_x, bottom_y, right_w, bottom_h, 12, theme.border);
    gfx_fill(right_x + 12, bottom_y + 12, 3, 12, theme.accent2);
    gfx_text(right_x + 22, bottom_y + 9, "BUILT FROM", theme.text);

    const char *modules[] = {
        "boot/     two-stage BIOS loader and VBE setup",
        "arch/     GDT, IDT, ISRs, PIC and INT 0x80",
        "drivers/  keyboard, mouse, timer, RTC, PCI, CPUID",
        "mm/       boundary-tag kernel heap",
        "fs/       hierarchical VFS and procfs",
        "proc/     PCBs and four scheduling policies",
        "gui/      software compositor and window manager",
        "apps/     shell, editor, files, monitor, DevTools"
    };
    int max_modules = (bottom_h - 70) / 20;
    if (max_modules > 8) max_modules = 8;
    if (max_modules < 0) max_modules = 0;
    gfx_set_clip(right_x + 16, bottom_y + 32, right_w - 30, bottom_h - 40);
    for (int i = 0; i < max_modules; i++)
        gfx_text(right_x + 22, bottom_y + 38 + i * 20, modules[i], theme.text_faint);
    gfx_reset_clip();

    int credits_y = bottom_y + bottom_h - 28;
    if (credits_y > bottom_y + 38 + max_modules * 20 + 4)
        gfx_text(right_x + 22, credits_y, "Built for people who want to read the kernel.",
                 theme.text_dim);

    /* Status. */
    char left[160];
    ksnprintf(left, sizeof(left), "%s  |  %u s uptime  |  %d windows",
              NOVA_CODENAME, timer_seconds(), wm_count());
    char right[80];
    ksnprintf(right, sizeof(right), "F1 help  |  T terminal");
    app_status(w, left, right, theme.text_faint);
}

static void about_mouse(window_t *w, int mx, int my, int button, int pressed)
{
    if (button != MOUSE_BTN_LEFT || !pressed) return;
    int ax = mx + CX(w);
    int ay = my + CY(w);
    int x = CX(w), y = CY(w), cw = CWD(w);

    int hero_y = y + 18;
    int bx = x + cw - 28 - 260;
    int by = hero_y + 26;

    if (app_hit(ax, ay, bx, by, 126, 30)) { app_open(APP_HELP); return; }
    if (app_hit(ax, ay, bx + 134, by, 126, 30)) { app_open(APP_TERMINAL); return; }
    by += 38;
    if (app_hit(ax, ay, bx, by, 126, 30)) { app_open(APP_MONITOR); return; }
    if (app_hit(ax, ay, bx + 134, by, 126, 30)) { app_open(APP_DEVTOOLS); return; }
}

static void about_key(window_t *w, const key_event_t *ev)
{
    UNUSED(w);
    switch (ev->ch) {
    case 't': case 'T': app_open(APP_TERMINAL); break;
    case 'h': case 'H': app_open(APP_HELP); break;
    case 'd': case 'D': app_open(APP_DEVTOOLS); break;
    case 'm': case 'M': app_open(APP_MONITOR); break;
    default: break;
    }
    if (ev->key == KEY_F1) app_open(APP_HELP);
}

static void about_close(window_t *w)
{
    if (w->state) {
        kfree(w->state);
        w->state = NULL;
    }
}

void app_about_open(void)
{
    window_t *w = wm_find(APP_ABOUT);
    if (w) {
        wm_focus(w);
        return;
    }

    about_state_t *s = (about_state_t *)kzalloc(sizeof(about_state_t));
    if (!s) return;

    int ww = min(860, max(640, gfx.width - 180));
    int wh = min(620, max(480, gfx.height - 170));
    int wx = max(30, (gfx.width - ww) / 2);
    int wy = max(50, (gfx.height - wh) / 2 - 8);

    window_t *win = wm_create(APP_ABOUT, "About NOVA", wx, wy, ww, wh,
                              600, 460, about_draw, about_key, about_mouse,
                              NULL, about_close, s);
    if (!win) kfree(s);
}
