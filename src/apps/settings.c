/* ============================================================================
 * src/apps/settings.c  -  Settings app: appearance, desktop and system control
 * ============================================================================ */

#include "kernel.h"
#include "theme.h"
#include "settings.h"
#include "gfx.h"
#include "widgets.h"
#include "mouse.h"
#include "timer.h"
#include "memory.h"
#include "process.h"
#include "vfs.h"
#include "system.h"
#include "cpu.h"
#include "interrupts.h"
#include "syscall.h"
#include "desktop.h"
#include "apps.h"
#include "apps_internal.h"

#define SET_SIDEBAR_W 158
#define SET_BAR_H     40

typedef struct {
    int section;
    int pending_power;      /* 1 = reboot, 2 = shutdown */
    u32 pending_tick;
} settings_app_state_t;

typedef struct {
    int cx, cy, cw, ch;

    /* Appearance */
    int accent_y, accent_h;
    int wallpaper_y, wallpaper_h;
    int theme_y, theme_h;

    /* Desktop */
    int behaviour_y, behaviour_h;
    int quick_y, quick_h;
    int workspace_y, workspace_h;

    /* System */
    int machine_y, machine_h;
    int services_y, services_h;
    int power_y, power_h;
} set_layout_t;

static const char *set_section_names[3] = { "Appearance", "Desktop", "System" };

/* â”€â”€ Layout â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void set_sidebar_rect(window_t *w, int index, int *x, int *y, int *bw, int *bh)
{
    if (x)  *x  = CX(w) + 8;
    if (y)  *y  = CY(w) + 52 + index * 42;
    if (bw) *bw = SET_SIDEBAR_W - 16;
    if (bh) *bh = 36;
}

static void set_layout(window_t *w, set_layout_t *l)
{
    l->cx = CX(w) + SET_SIDEBAR_W + 1;
    l->cy = CY(w) + SET_BAR_H;
    l->cw = CWD(w) - SET_SIDEBAR_W - 1;
    l->ch = CHT(w) - SET_BAR_H - APP_STATUS_H;

    int y = l->cy + 22;
    l->accent_y = y;      l->accent_h = 98;
    l->wallpaper_y = y + l->accent_h + 14;  l->wallpaper_h = 148;
    l->theme_y = l->wallpaper_y + l->wallpaper_h + 14; l->theme_h = 92;

    l->behaviour_y = y;   l->behaviour_h = 164;
    l->quick_y = l->behaviour_y + l->behaviour_h + 14; l->quick_h = 132;
    l->workspace_y = l->quick_y + l->quick_h + 14; l->workspace_h = 96;

    l->machine_y = y;     l->machine_h = 176;
    l->services_y = l->machine_y + l->machine_h + 14; l->services_h = 112;
    l->power_y = l->services_y + l->services_h + 14;  l->power_h = 90;
}

static int set_row_y(set_layout_t *l, int card_y, int index)
{
    return card_y + 36 + index * 29;
}

/* â”€â”€ Small drawing helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void set_card(int x, int y, int w, int h)
{
    gfx_round_rect(x, y, w, h, 12, theme.panel);
    gfx_round_outline(x, y, w, h, 12, theme.border);
}

static void set_card_title(int x, int y, int w, const char *title, const char *hint)
{
    gfx_fill(x + 14, y + 12, 3, 12, theme.accent);
    gfx_text(x + 24, y + 9, title, theme.text);
    if (hint) gfx_text_right(x + w - 16, y + 9, hint, theme.text_faint);
}

static void set_setting_row(int x, int y, int w, const char *label,
                            const char *value, int on)
{
    gfx_text(x + 20, y + 4, label, theme.text_dim);
    if (value) gfx_text_right(x + w - 76, y + 4, value,
                              on ? theme.success : theme.text_faint);
    ui_toggle(x + w - 58, y + 2, 38, 20, on, NULL);
}

static void set_accent_swatch(int x, int y, int index, int selected, int hot)
{
    u32 color = accent_colors[index];
    int size = 34;

    if (selected)
        gfx_circle_ring(x + size / 2, y + size / 2, 22, 2, color);

    gfx_circle_fill(x + size / 2, y + size / 2, size / 2, color);
    if (hot)
        gfx_circle_ring(x + size / 2, y + size / 2, 20, 2,
                        gfx_lerp(color, 0xFFFFFF, 80));

    const char *name = accent_names[index];
    int nw = gfx_text_width(name);
    gfx_text(x + (size - nw) / 2, y + size + 8, name,
             selected ? theme.text : theme.text_faint);
}

static void set_wallpaper_preview(int x, int y, int w, int h, int index, int selected,
                                  int hot)
{
    gfx_set_clip(x, y, w, h);

    u32 top = theme.bg0;
    u32 bottom = theme.bg1;
    u32 glow = theme.accent;
    u32 glow2 = theme.accent2;

    if (index == 2) {
        top = 0x05070E;
        bottom = 0x180B2C;
        glow = 0x8B5CF6;
        glow2 = 0x06B6D4;
    } else if (index == 3) {
        top = 0x080C15;
        bottom = 0x121A2B;
        glow = theme.accent;
        glow2 = 0x64748B;
    }

    gfx_gradient_v(x, y, w, h, top, bottom);
    gfx_fill_blend(x + w / 5, y + h / 5, w / 2, h / 2, glow, 46);
    gfx_fill_blend(x + w / 2, y + h / 3, w / 3, h / 3, glow2, 38);

    if (index == 1) {
        for (int gx = 0; gx < w; gx += 18)
            gfx_fill(x + gx, y, 1, h, gfx_lerp(top, glow, 34));
        for (int gy = 0; gy < h; gy += 18)
            gfx_fill(x, y + gy, w, 1, gfx_lerp(top, glow, 28));
    } else {
        for (int i = 0; i < 14; i++) {
            int sx = x + (i * 53 + 17) % max(1, w);
            int sy = y + (i * 37 + 11) % max(1, h);
            gfx_blend_pixel(sx, sy, 0xFFFFFF, 80);
        }
    }

    gfx_reset_clip();

    gfx_round_outline(x, y, w, h, 8,
                      selected ? theme.accent : (hot ? theme.text_dim : theme.border));

    if (selected)
        gfx_round_outline(x - 2, y - 2, w + 4, h + 4, 10, theme.accent);

    gfx_text(x + 8, y + h - 20, wallpaper_names[index],
             selected ? theme.text : theme.text_dim);
}

static void set_open_app_button(int x, int y, int w, const char *label,
                                app_id_t app, int hot)
{
    u32 base = hot ? gfx_lerp(theme.panel2, 0xFFFFFF, 18) : theme.panel2;
    gfx_round_rect(x, y, w, 32, 8, base);
    gfx_round_outline(x, y, w, 32, 8, hot ? theme.accent : theme.border);
    app_icon_draw(app, x + 7, y + 4, 24, 0);
    gfx_text(x + 38, y + 8, label, theme.text_dim);
}

/* â”€â”€ Appearance â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void set_draw_appearance(window_t *w, settings_app_state_t *s, set_layout_t *l)
{
    UNUSED(w); UNUSED(s);
    int x = l->cx, cw = l->cw;
    mouse_state_t *m = mouse_get();
    int mx = m ? m->x : -1, my = m ? m->y : -1;

    /* Accent card. */
    int y = l->accent_y;
    set_card(x + 12, y, cw - 24, l->accent_h);
    set_card_title(x + 12, y, cw - 24, "ACCENT COLOUR", "used across the desktop");
    for (int i = 0; i < 5; i++) {
        int sx = x + 34 + i * 60;
        int sy = y + 40;
        int hot = app_hit(mx, my, sx - 9, sy - 9, 52, 74);
        set_accent_swatch(sx, sy, i, g_settings.accent == i, hot);
    }

    /* Wallpaper card. */
    y = l->wallpaper_y;
    set_card(x + 12, y, cw - 24, l->wallpaper_h);
    set_card_title(x + 12, y, cw - 24, "WALLPAPER", "rendered live in 32-bit colour");

    int pad = 20;
    int gap = 10;
    int pw = (cw - 24 - pad * 2 - gap * 3) / 4;
    if (pw < 70) pw = 70;
    int ph = 82;
    for (int i = 0; i < 4; i++) {
        int px = x + 12 + pad + i * (pw + gap);
        int py = y + 36;
        int hot = app_hit(mx, my, px, py, pw, ph);
        set_wallpaper_preview(px, py, pw, ph, i, g_settings.wallpaper == i, hot);
    }

    /* Theme summary and save. */
    y = l->theme_y;
    set_card(x + 12, y, cw - 24, l->theme_h);
    set_card_title(x + 12, y, cw - 24, "CURRENT THEME", "/etc/nova.conf");

    gfx_fill(x + 32, y + 42, 4, 30,
             gfx_lerp(theme.accent, theme.accent2, 120));
    gfx_text(x + 48, y + 38, accent_names[g_settings.accent], theme.text);
    gfx_textf(x + 48, y + 56, theme.text_dim, "%s wallpaper  |  %ux%u display",
              wallpaper_names[g_settings.wallpaper], gfx.width, gfx.height);

    int bx = x + cw - 24 - 210;
    int by = y + 40;
    int hot = app_hit(mx, my, bx, by, 96, 30);
    ui_button(bx, by, 96, 30, "Save", hot, 1);
    hot = app_hit(mx, my, bx + 106, by, 96, 30);
    ui_button(bx + 106, by, 96, 30, "Reset", hot, 0);
}

/* â”€â”€ Desktop â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void set_draw_desktop(window_t *w, settings_app_state_t *s, set_layout_t *l)
{
    UNUSED(w); UNUSED(s);
    int x = l->cx, cw = l->cw;
    mouse_state_t *m = mouse_get();
    int mx = m ? m->x : -1, my = m ? m->y : -1;

    /* Interface behaviour. */
    int y = l->behaviour_y;
    set_card(x + 12, y, cw - 24, l->behaviour_h);
    set_card_title(x + 12, y, cw - 24, "INTERFACE", "changes apply immediately");

    set_setting_row(x + 12, set_row_y(l, y, 0), cw - 24, "Animations",
                    g_settings.animations ? "on" : "off", g_settings.animations);
    set_setting_row(x + 12, set_row_y(l, y, 1), cw - 24, "Dock magnification",
                    g_settings.dock_magnify ? "on" : "off", g_settings.dock_magnify);
    set_setting_row(x + 12, set_row_y(l, y, 2), cw - 24, "FPS counter",
                    g_settings.show_fps ? "on" : "off", g_settings.show_fps);
    set_setting_row(x + 12, set_row_y(l, y, 3), cw - 24, "Clock seconds",
                    g_settings.clock_seconds ? "on" : "off", g_settings.clock_seconds);

    /* Quick actions. */
    y = l->quick_y;
    set_card(x + 12, y, cw - 24, l->quick_h);
    set_card_title(x + 12, y, cw - 24, "QUICK ACTIONS", "open a workspace tool");

    int bw = (cw - 24 - 40 - 10) / 2;
    int bx = x + 32;
    int by = y + 38;
    set_open_app_button(bx, by, bw, "Terminal", APP_TERMINAL,
                        app_hit(mx, my, bx, by, bw, 32));
    set_open_app_button(bx + bw + 10, by, bw, "Files", APP_FILES,
                        app_hit(mx, my, bx + bw + 10, by, bw, 32));
    by += 38;
    set_open_app_button(bx, by, bw, "DevTools", APP_DEVTOOLS,
                        app_hit(mx, my, bx, by, bw, 32));
    set_open_app_button(bx + bw + 10, by, bw, "System Monitor", APP_MONITOR,
                        app_hit(mx, my, bx + bw + 10, by, bw, 32));
    by += 38;
    int snap_w = cw - 64;
    int hot = app_hit(mx, my, bx, by, snap_w, 30);
    ui_button(bx, by, snap_w, 30, "Re-center windows in the workspace", hot, 0);

    /* Workspace summary. */
    y = l->workspace_y;
    set_card(x + 12, y, cw - 24, l->workspace_h);
    set_card_title(x + 12, y, cw - 24, "WORKSPACE", "desktop geometry");
    int wx, wy, ww, wh;
    desktop_workspace(&wx, &wy, &ww, &wh);
    gfx_textf(x + 32, y + 38, theme.text_dim,
              "  usable area  %d x %d at (%d, %d)", ww, wh, wx, wy);
    gfx_textf(x + 32, y + 58, theme.text_faint,
              "  %d window%s open  |  Super: launcher  |  Alt+Tab: cycle",
              wm_count(), wm_count() == 1 ? "" : "s");
}

/* â”€â”€ System & power â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void set_draw_system(window_t *w, settings_app_state_t *s, set_layout_t *l)
{
    UNUSED(w);
    int x = l->cx, cw = l->cw;
    mouse_state_t *m = mouse_get();
    int mx = m ? m->x : -1, my = m ? m->y : -1;

    cpu_info_t cpu;
    cpuid_info(&cpu);
    char brand[64];
    kstrncpy(brand, cpu.brand, sizeof(brand));
    brand[sizeof(brand) - 1] = '\0';

    mem_info_t mi;
    mem_info(&mi);

    int y = l->machine_y;
    set_card(x + 12, y, cw - 24, l->machine_h);
    set_card_title(x + 12, y, cw - 24, "THIS MACHINE", "NOVA " NOVA_VERSION " " NOVA_CODENAME);

    gfx_text(x + 30, y + 38, "Operating system", theme.text_faint);
    gfx_text(x + 208, y + 38, "Altitude OS " NOVA_VERSION " (" NOVA_CODENAME ")",
             theme.text);
    gfx_text(x + 30, y + 60, "Architecture", theme.text_faint);
    gfx_text(x + 208, y + 60, system_arch(), theme.text_dim);
    gfx_text(x + 30, y + 82, "Processor", theme.text_faint);
    gfx_set_clip(x + 208, y + 82, cw - 250, 16);
    gfx_text(x + 208, y + 82, brand[0] ? brand : "Generic x86", theme.text_dim);
    gfx_reset_clip();
    gfx_text(x + 30, y + 104, "Memory", theme.text_faint);
    gfx_textf(x + 208, y + 104, theme.text_dim, "%u MiB detected", system_total_ram_kb() / 1024);
    gfx_text(x + 30, y + 126, "Display", theme.text_faint);
    gfx_textf(x + 208, y + 126, theme.text_dim, "%ux%u @ %ubpp  |  VBE linear",
              gfx.width, gfx.height, gfx.bpp);
    gfx_text(x + 30, y + 148, "Heap", theme.text_faint);
    gfx_textf(x + 208, y + 148, theme.text_dim, "%u / %u KiB used",
              mi.used / 1024, mi.total / 1024);

    /* Service counters. */
    y = l->services_y;
    set_card(x + 12, y, cw - 24, l->services_h);
    set_card_title(x + 12, y, cw - 24, "SYSTEM SERVICES", "live counters");
    int half = (cw - 24) / 2;
    gfx_textf(x + 30, y + 38, theme.text_dim, "VFS nodes     %d / %d",
              vfs_used_nodes(), VFS_MAX_NODES);
    gfx_textf(x + 30, y + 60, theme.text_dim, "Processes     %d active",
              proc_active_count());
    gfx_textf(x + 30, y + 82, theme.text_dim, "Scheduler     %s", sched_algo_name());
    gfx_textf(x + 30 + half, y + 38, theme.text_dim, "Uptime        %u s", timer_seconds());
    gfx_textf(x + 30 + half, y + 60, theme.text_dim, "IRQs          %u",
              interrupt_total_count());
    gfx_textf(x + 30 + half, y + 82, theme.text_dim, "Syscalls      %u",
              syscall_count());

    /* Power. */
    y = l->power_y;
    set_card(x + 12, y, cw - 24, l->power_h);
    set_card_title(x + 12, y, cw - 24, "POWER", "saves current preferences first");
    gfx_text(x + 30, y + 36, "A second click confirms the action.",
             theme.text_faint);

    int bw = 132;
    int bx = x + 30;
    int by = y + 56;
    int hot = app_hit(mx, my, bx, by, bw, 30);
    const char *reboot_label = (s->pending_power == 1) ? "Confirm reboot" : "Reboot";
    ui_button(bx, by, bw, 30, reboot_label, hot, s->pending_power == 1);

    bx += bw + 12;
    hot = app_hit(mx, my, bx, by, bw, 30);
    const char *halt_label = (s->pending_power == 2) ? "Confirm shutdown" : "Shut down";
    ui_button(bx, by, bw, 30, halt_label, hot, 0);
}

/* â”€â”€ Main drawing â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void set_draw(window_t *w)
{
    settings_app_state_t *s = (settings_app_state_t *)w->state;
    if (!s) return;

    int x = CX(w), y = CY(w), cw = CWD(w), ch = CHT(w);
    gfx_fill(x, y, cw, ch, theme.win);

    /* Toolbar. */
    gfx_fill(x, y, cw, SET_BAR_H, theme.panel);
    gfx_fill(x, y + SET_BAR_H - 1, cw, 1, theme.border);
    gfx_fill(x + 10, y + 13, 4, 14, theme.accent);
    gfx_text(x + 22, y + 12, "Settings", theme.text);
    gfx_text(x + 96, y + 12, "preferences, appearance and power", theme.text_faint);
    gfx_text_right(x + cw - 14, y + 12, NOVA_VERSION " " NOVA_CODENAME, theme.text_faint);

    /* Sidebar. */
    mouse_state_t *m = mouse_get();
    int mx = m ? m->x : -1, my = m ? m->y : -1;

    int side_y = y + SET_BAR_H;
    int side_h = ch - SET_BAR_H - APP_STATUS_H;
    gfx_fill(x, side_y, SET_SIDEBAR_W, side_h, theme.panel);
    gfx_fill(x + SET_SIDEBAR_W, side_y, 1, side_h, theme.border);
    gfx_text(x + 18, side_y + 12, "SETTINGS", theme.text_faint);

    for (int i = 0; i < 3; i++) {
        int bx, by, bw, bh;
        set_sidebar_rect(w, i, &bx, &by, &bw, &bh);
        (void)bx; (void)bw; (void)bh;

        int selected = (s->section == i);
        int hot = app_hit(mx, my, x + 8, by, SET_SIDEBAR_W - 16, 36);
        if (selected)
            gfx_round_rect(x + 8, by, SET_SIDEBAR_W - 16, 36, 8, theme.accent_soft);
        else if (hot)
            gfx_round_rect(x + 8, by, SET_SIDEBAR_W - 16, 36, 8, theme.panel2);

        u32 color = selected ? theme.accent : theme.text_faint;
        if (i == 0) {
            gfx_circle_fill(x + 24, by + 18, 6, color);
            gfx_circle_ring(x + 24, by + 18, 10, 1, color);
        } else if (i == 1) {
            gfx_round_rect(x + 15, by + 11, 18, 13, 2, color);
            gfx_fill(x + 21, by + 24, 6, 2, color);
        } else {
            gfx_circle_ring(x + 24, by + 18, 8, 2, color);
            gfx_fill(x + 23, by + 9, 2, 8, color);
        }

        gfx_text(x + 42, by + 10, set_section_names[i],
                 selected ? theme.text : theme.text_dim);
    }

    gfx_fill(x + 14, side_y + side_h - 54, SET_SIDEBAR_W - 28, 1, theme.border);
    gfx_text(x + 18, side_y + side_h - 40, "Altitude OS", theme.text_dim);
    gfx_text(x + 18, side_y + side_h - 22, "programmer edition", theme.text_faint);

    /* Content. */
    set_layout_t l;
    set_layout(w, &l);

    if (s->section == 0)      set_draw_appearance(w, s, &l);
    else if (s->section == 1) set_draw_desktop(w, s, &l);
    else                      set_draw_system(w, s, &l);

    char left[160];
    ksnprintf(left, sizeof(left), "%s  |  changes are written to /etc/nova.conf",
              set_section_names[s->section]);
    app_status(w, left, "Esc: close  1-3: section", theme.text_faint);
}

/* â”€â”€ Actions â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void set_save(const char *what)
{
    if (settings_save() == 0)
        toast_show("Settings", what ? what : "Preferences saved", TOAST_SUCCESS);
    else
        toast_show("Settings", "Could not write /etc/nova.conf", TOAST_ERROR);
}

static void set_apply_accent(settings_app_state_t *s, int index)
{
    UNUSED(s);
    g_settings.accent = index;
    settings_apply();
    set_save(accent_names[index]);
}

static void set_apply_wallpaper(int index)
{
    g_settings.wallpaper = index;
    settings_apply();
    set_save(wallpaper_names[index]);
}

static void set_toggle(settings_app_state_t *s, int which)
{
    UNUSED(s);
    switch (which) {
    case 0: g_settings.animations = !g_settings.animations; break;
    case 1: g_settings.dock_magnify = !g_settings.dock_magnify; break;
    case 2: g_settings.show_fps = !g_settings.show_fps; break;
    case 3: g_settings.clock_seconds = !g_settings.clock_seconds; break;
    default: return;
    }
    settings_save();
    toast_show("Desktop", "Preference updated", TOAST_INFO);
}

static void set_power(settings_app_state_t *s, int kind)
{
    u32 now = timer_ticks();
    if (s->pending_power != kind || now - s->pending_tick > 220) {
        s->pending_power = kind;
        s->pending_tick = now;
        toast_show("Power", kind == 1 ? "Click Reboot again to confirm"
                                      : "Click Shut down again to confirm",
                   TOAST_WARN);
        return;
    }

    settings_save();
    if (kind == 1) app_power_reboot();
    else           app_power_halt();
}

/* â”€â”€ Input â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void set_mouse(window_t *w, int mx, int my, int button, int pressed)
{
    settings_app_state_t *s = (settings_app_state_t *)w->state;
    if (!s || button != MOUSE_BTN_LEFT || !pressed) return;

    int ax = mx + CX(w);
    int ay = my + CY(w);

    for (int i = 0; i < 3; i++) {
        int bx, by, bw, bh;
        set_sidebar_rect(w, i, &bx, &by, &bw, &bh);
        if (app_hit(ax, ay, bx, by, bw, bh)) {
            s->section = i;
            return;
        }
    }

    set_layout_t l;
    set_layout(w, &l);

    if (s->section == 0) {
        for (int i = 0; i < 5; i++) {
            int sx = l.cx + 34 + i * 60;
            int sy = l.accent_y + 40;
            if (app_hit(ax, ay, sx - 9, sy - 9, 52, 74)) {
                set_apply_accent(s, i);
                return;
            }
        }

        int pad = 20, gap = 10;
        int pw = (l.cw - 24 - pad * 2 - gap * 3) / 4;
        if (pw < 70) pw = 70;
        for (int i = 0; i < 4; i++) {
            int px = l.cx + 12 + pad + i * (pw + gap);
            int py = l.wallpaper_y + 36;
            if (app_hit(ax, ay, px, py, pw, 82)) {
                set_apply_wallpaper(i);
                return;
            }
        }

        int bx = l.cx + l.cw - 24 - 210;
        int by = l.theme_y + 40;
        if (app_hit(ax, ay, bx, by, 96, 30)) { set_save("Preferences saved"); return; }
        if (app_hit(ax, ay, bx + 106, by, 96, 30)) {
            g_settings.accent = 0;
            g_settings.wallpaper = 0;
            g_settings.animations = 1;
            g_settings.dock_magnify = 1;
            g_settings.show_fps = 0;
            g_settings.clock_seconds = 0;
            settings_apply();
            set_save("Defaults restored");
            return;
        }
    } else if (s->section == 1) {
        for (int i = 0; i < 4; i++) {
            int ry = set_row_y(&l, l.behaviour_y, i);
            if (app_hit(ax, ay, l.cx + 12, ry - 2, l.cw - 24, 26)) {
                set_toggle(s, i);
                return;
            }
        }

        int bw = (l.cw - 24 - 40 - 10) / 2;
        int bx = l.cx + 32;
        int by = l.quick_y + 38;
        if (app_hit(ax, ay, bx, by, bw, 32)) { app_open(APP_TERMINAL); return; }
        if (app_hit(ax, ay, bx + bw + 10, by, bw, 32)) { app_open(APP_FILES); return; }
        by += 38;
        if (app_hit(ax, ay, bx, by, bw, 32)) { app_open(APP_DEVTOOLS); return; }
        if (app_hit(ax, ay, bx + bw + 10, by, bw, 32)) { app_open(APP_MONITOR); return; }
        by += 38;
        if (app_hit(ax, ay, bx, by, l.cw - 64, 30)) {
            wm_snap_workspace();
            toast_show("Workspace", "Windows re-centered", TOAST_SUCCESS);
            return;
        }
    } else {
        int by = l.power_y + 56;
        if (app_hit(ax, ay, l.cx + 30, by, 132, 30)) { set_power(s, 1); return; }
        if (app_hit(ax, ay, l.cx + 30 + 144, by, 132, 30)) { set_power(s, 2); return; }
    }
}

static void set_key(window_t *w, const key_event_t *ev)
{
    settings_app_state_t *s = (settings_app_state_t *)w->state;
    if (!s) return;

    if (ev->ch >= '1' && ev->ch <= '3') s->section = ev->ch - '1';

    if (ev->ch == 's' || ev->ch == 'S') set_save("Preferences saved");

    if (ev->ch == 'a' || ev->ch == 'A') {
        set_apply_accent(s, (g_settings.accent + 1) % 5);
        return;
    }
    if (ev->ch == 'w' || ev->ch == 'W') {
        set_apply_wallpaper((g_settings.wallpaper + 1) % 4);
        return;
    }

    switch (ev->key) {
    case KEY_UP:
        if (s->section > 0) s->section--;
        break;
    case KEY_DOWN:
        if (s->section < 2) s->section++;
        break;
    case KEY_TAB:
        s->section = (s->section + 1) % 3;
        break;
    default:
        break;
    }
}

static void set_close(window_t *w)
{
    if (w->state) {
        kfree(w->state);
        w->state = NULL;
    }
}

void app_settings_open(void)
{
    window_t *w = wm_find(APP_SETTINGS);
    if (w) {
        wm_focus(w);
        return;
    }

    settings_app_state_t *s = (settings_app_state_t *)kzalloc(sizeof(settings_app_state_t));
    if (!s) {
        toast_show("Settings", "Not enough memory", TOAST_ERROR);
        return;
    }

    int ww = min(850, max(650, gfx.width - 220));
    int wh = min(660, max(540, gfx.height - 150));
    int wx = max(30, (gfx.width - ww) / 2 - 10);
    int wy = max(48, (gfx.height - wh) / 2 - 8);

    window_t *win = wm_create(APP_SETTINGS, "Settings", wx, wy, ww, wh,
                              620, 520, set_draw, set_key, set_mouse,
                              NULL, set_close, s);
    if (!win) kfree(s);
}
