/* ============================================================================
 * src/apps/apps.c  -  Application registry, icons, launch dispatch
 * ============================================================================ */

#include "kernel.h"
#include "apps.h"
#include "apps_internal.h"
#include "desktop.h"
#include "theme.h"
#include "gfx.h"
#include "widgets.h"
#include "timer.h"
#include "memory.h"
#include "process.h"
#include "system.h"
#include "serial.h"

/* â”€â”€ Registry â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static const app_info_t app_infos[APP_COUNT] = {
    [APP_TERMINAL]   = { "Terminal",   "terminal",   "Interactive shell and developer console", 0x22D3EE },
    [APP_FILES]      = { "Files",      "files",      "Browse and manage the NOVA filesystem",   0xFBBF24 },
    [APP_EDITOR]     = { "Text Editor","editor",     "Edit source files and notes",             0xA78BFA },
    [APP_MONITOR]    = { "System Monitor", "monitor","Live memory, CPU and interrupt graphs",   0x34D399 },
    [APP_PROCESS]    = { "Process Manager", "process","Process table and scheduler laboratory", 0xFB7185 },
    [APP_CALCULATOR] = { "Calculator", "calculator", "Integer expression calculator",           0xF59E0B },
    [APP_DEVTOOLS]   = { "DevTools",   "devtools",   "Kernel explorer: registers, IDT, PCI",    0x818CF8 },
    [APP_SETTINGS]   = { "Settings",   "settings",   "Appearance, wallpaper and power",         0x94A3B8 },
    [APP_ABOUT]      = { "About NOVA", "about",      "Version, credits and system facts",       0x38BDF8 },
    [APP_HELP]       = { "Help",       "help",       "Shortcuts and developer guide",           0x22C55E }
};

const app_info_t *app_info(app_id_t id)
{
    if (id < 0 || id >= APP_COUNT) return &app_infos[APP_TERMINAL];
    return &app_infos[id];
}

int app_id_from_name(const char *name)
{
    if (!name || !*name) return -1;

    for (int i = 0; i < APP_COUNT; i++) {
        if (kstrcasecmp(name, app_infos[i].short_name) == 0 ||
            kstrcasecmp(name, app_infos[i].name) == 0)
            return i;
    }

    if (kstrcasecmp(name, "proc") == 0) return APP_PROCESS;
    if (kstrcasecmp(name, "sysmon") == 0) return APP_MONITOR;
    if (kstrcasecmp(name, "dev") == 0) return APP_DEVTOOLS;
    if (kstrcasecmp(name, "text") == 0) return APP_EDITOR;
    if (kstrcasecmp(name, "calc") == 0) return APP_CALCULATOR;
    if (kstrcasecmp(name, "config") == 0) return APP_SETTINGS;
    if (kstrcasecmp(name, "shell") == 0) return APP_TERMINAL;
    if (kstrcasecmp(name, "filesystem") == 0) return APP_FILES;

    return -1;
}

void app_open(app_id_t id)
{
    switch (id) {
    case APP_TERMINAL:   app_terminal_open();   break;
    case APP_FILES:      app_files_open();      break;
    case APP_EDITOR:     app_editor_open();     break;
    case APP_MONITOR:    app_monitor_open();    break;
    case APP_PROCESS:    app_process_open();    break;
    case APP_CALCULATOR: app_calculator_open(); break;
    case APP_DEVTOOLS:   app_devtools_open();   break;
    case APP_SETTINGS:   app_settings_open();   break;
    case APP_ABOUT:      app_about_open();      break;
    case APP_HELP:       app_help_open();       break;
    default: break;
    }
}

void app_open_named(const char *name)
{
    int id = app_id_from_name(name);
    if (id >= 0) app_open((app_id_t)id);
}

/* â”€â”€ Cross-app actions â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

void app_editor_open_file(const char *path) { app_editor_load(path); }
void app_files_open_path(const char *path)  { app_files_open_at(path); }
void app_terminal_run(const char *command)  { app_terminal_execute(command); }
void app_terminal_open_with(const char *cwd) { app_terminal_open_at(cwd); }

/* â”€â”€ Icons â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void thick_line(int x0, int y0, int x1, int y1, u32 color, int t)
{
    if (t < 1) t = 1;
    for (int i = 0; i < t; i++) gfx_line(x0 + i, y0, x1 + i, y1, color);
}

static void icon_glyph(app_id_t id, int x, int y, int size, u32 fg, u32 bg)
{
    #define G(v) ((v) * size / 48)
    int t = max(1, size / 24);

    switch (id) {
    case APP_TERMINAL:
        thick_line(x + G(14), y + G(16), x + G(21), y + G(24), fg, t);
        thick_line(x + G(21), y + G(24), x + G(14), y + G(32), fg, t);
        gfx_fill(x + G(25), y + G(31), G(10), t, fg);
        break;

    case APP_FILES:
        gfx_round_rect(x + G(10), y + G(13), G(18), G(9), G(3), fg);
        gfx_round_rect(x + G(8), y + G(20), G(32), G(19), G(4), fg);
        gfx_fill(x + G(12), y + G(25), G(24), t, gfx_lerp(fg, bg, 95));
        break;

    case APP_EDITOR:
        gfx_round_rect(x + G(13), y + G(9), G(22), G(30), G(3), fg);
        for (int r = 0; r < 4; r++)
            gfx_fill(x + G(17), y + G(16 + r * 5), G(11 - r), t,
                     gfx_lerp(fg, bg, 120));
        break;

    case APP_MONITOR:
        gfx_round_rect(x + G(8), y + G(11), G(32), G(22), G(3), fg);
        gfx_round_rect(x + G(11), y + G(14), G(26), G(16), G(2),
                       gfx_lerp(fg, bg, 150));
        thick_line(x + G(14), y + G(24), x + G(18), y + G(19), fg, t);
        thick_line(x + G(18), y + G(19), x + G(22), y + G(25), fg, t);
        thick_line(x + G(22), y + G(25), x + G(27), y + G(17), fg, t);
        thick_line(x + G(27), y + G(17), x + G(33), y + G(22), fg, t);
        gfx_fill(x + G(21), y + G(34), G(6), G(3), fg);
        break;

    case APP_PROCESS:
        for (int r = 0; r < 3; r++) {
            int yy = 15 + r * 8;
            gfx_circle_fill(x + G(14), y + G(yy), max(1, G(2)), fg);
            gfx_fill(x + G(20), y + G(yy - 2), G(18 - r * 3), G(3), fg);
        }
        break;

    case APP_CALCULATOR:
        gfx_round_rect(x + G(11), y + G(9), G(26), G(30), G(3), fg);
        gfx_round_rect(x + G(14), y + G(12), G(20), G(7), G(2),
                       gfx_lerp(fg, bg, 160));
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                gfx_circle_fill(x + G(17 + c * 7), y + G(24 + r * 5),
                                max(1, G(2)), gfx_lerp(fg, bg, 100));
        break;

    case APP_DEVTOOLS:
        thick_line(x + G(18), y + G(15), x + G(11), y + G(24), fg, t);
        thick_line(x + G(11), y + G(24), x + G(18), y + G(33), fg, t);
        thick_line(x + G(30), y + G(15), x + G(37), y + G(24), fg, t);
        thick_line(x + G(37), y + G(24), x + G(30), y + G(33), fg, t);
        thick_line(x + G(27), y + G(14), x + G(21), y + G(34),
                   gfx_lerp(fg, bg, 70), t);
        break;

    case APP_SETTINGS: {
        int cx = x + G(24), cy = y + G(24);
        static const int dirs[8][2] = {
            { 0, -1 }, { 1, -1 }, { 1, 0 }, { 1, 1 },
            { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }
        };
        gfx_circle_ring(cx, cy, G(13), max(2, G(4)), fg);
        for (int i = 0; i < 8; i++) {
            int x0 = cx + dirs[i][0] * G(13);
            int y0 = cy + dirs[i][1] * G(13);
            int x1 = cx + dirs[i][0] * G(20);
            int y1 = cy + dirs[i][1] * G(20);
            thick_line(x0, y0, x1, y1, fg, max(1, t));
        }
        gfx_circle_fill(cx, cy, G(4), bg);
        break;
    }

    case APP_ABOUT: {
        int cx = x + G(24), cy = y + G(24);
        gfx_circle_ring(cx, cy, G(18), max(2, G(2)), fg);
        gfx_circle_fill(cx, cy - G(8), G(2), fg);
        gfx_fill(cx - G(1), cy - G(3), G(2), G(13), fg);
        break;
    }

    case APP_HELP: {
        int cx = x + G(24), cy = y + G(24);
        gfx_circle_ring(cx, cy, G(18), max(2, G(2)), fg);
        int scale = size >= 40 ? 2 : 1;
        int tw = gfx_text_width_scale("?", scale);
        gfx_text_scale(cx - tw / 2, cy - (16 * scale) / 2 - G(1),
                       "?", fg, scale);
        break;
    }

    default:
        gfx_circle_fill(x + G(24), y + G(24), G(10), fg);
        break;
    }

    #undef G
}

void app_icon_draw(app_id_t id, int x, int y, int size, int active)
{
    if (size < 8) size = 8;
    const app_info_t *info = app_info(id);

    int radius = max(4, size / 5);
    u32 base = gfx_lerp(info->color, 0x04080F, 165);
    u32 edge = active ? theme.accent : gfx_lerp(info->color, 0x000000, 78);
    u32 fg = gfx_lerp(info->color, 0xFFFFFF, 175);

    gfx_round_rect(x, y, size, size, radius, base);
    gfx_round_rect_blend(x + 1, y + 1, size - 2, size / 2,
                         max(2, radius - 1), info->color, 20);
    gfx_round_outline(x, y, size, size, radius, edge);

    icon_glyph(id, x, y, size, fg, base);
}

void app_icon_draw_plain(app_id_t id, int x, int y, int size, u32 color)
{
    icon_glyph(id, x, y, size, color, 0x04080F);
}

/* â”€â”€ Shared app chrome â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

int app_hit(int mx, int my, int x, int y, int w, int h)
{
    return mx >= x && my >= y && mx < x + w && my < y + h;
}

void app_bar(window_t *w, int height, const char *left, const char *right)
{
    int x = CX(w), y = CY(w), cw = CWD(w);
    gfx_fill(x, y, cw, height, theme.panel);
    gfx_fill(x, y + height - 1, cw, 1, theme.border);
    gfx_fill(x + 10, y + (height - 12) / 2, 4, 12, theme.accent);
    if (left) gfx_text(x + 22, y + (height - 16) / 2, left, theme.text);
    if (right) gfx_text_right(x + cw - 12, y + (height - 16) / 2, right, theme.text_faint);
}

void app_status(window_t *w, const char *left, const char *right, u32 right_color)
{
    int x = CX(w);
    int y = CY(w) + CHT(w) - APP_STATUS_H;
    int cw = CWD(w);
    gfx_fill(x, y, cw, APP_STATUS_H, theme.panel);
    gfx_fill(x, y, cw, 1, theme.border);
    if (left) gfx_text(x + 10, y + 3, left, theme.text_faint);
    if (right) gfx_text_right(x + cw - 10, y + 3, right, right_color);
}

/* â”€â”€ Periodic hooks â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static int auto_sched = 1;

void apps_on_tick(u32 ticks)
{
    UNUSED(ticks);
}

int apps_auto_tick_due(void)
{
    return auto_sched;
}

void apps_on_second(void)
{
    monitor_second();
}

void app_set_auto_sched(int on) { auto_sched = on ? 1 : 0; }
int  app_get_auto_sched(void)   { return auto_sched; }

/* â”€â”€ Power actions â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

void app_power_reboot(void)
{
    toast_show("Rebooting", "Altitude OS will restart in a moment.", TOAST_INFO);
    timer_sleep_ms(650);
    serial_write("reboot requested by user\n");
    reboot_machine();
}

void app_power_halt(void)
{
    toast_show("Shutting down", "It is now safe to close QEMU.", TOAST_WARN);
    timer_sleep_ms(650);
    serial_write("halt requested by user\n");
    halt_machine();
}
