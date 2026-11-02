/* ============================================================================
 * src/gui/desktop.c  -  Desktop compositor: panel, dock, launcher, overlays
 *
 * The compositor owns the main loop.  Every frame it restores the cached
 * wallpaper into the back-buffer, paints the desktop layer, asks the window
 * manager to draw its windows, and finally draws the shell chrome (top bar,
 * dock, menus, toasts, cursor) on top.
 *
 * Input is polled from the IRQ-driven keyboard and mouse drivers.  Shell
 * overlays get the first chance to consume events; otherwise they are routed
 * to the window manager and the focused application.
 * ============================================================================ */

#include "kernel.h"
#include "desktop.h"
#include "window.h"
#include "apps.h"
#include "theme.h"
#include "settings.h"
#include "gfx.h"
#include "widgets.h"
#include "mouse.h"
#include "keyboard.h"
#include "keys.h"
#include "timer.h"
#include "rtc.h"
#include "memory.h"
#include "process.h"
#include "system.h"
#include "serial.h"

/* ── Shell metrics ────────────────────────────────────────────────────────── */

#define BAR_H           34
#define DOCK_H          66
#define DOCK_BOTTOM     10
#define DOCK_ICON       42
#define DOCK_PAD        9
#define DOCK_GAP        6

#define DESK_ICON       48
#define DESK_CELL_W     96
#define DESK_CELL_H     90
#define DESK_LEFT       20
#define DESK_TOP        (BAR_H + 20)

#define TOAST_W         292
#define TOAST_H         54
#define TOAST_GAP       8
#define TOAST_LIFE_MS   4500
#define TOAST_MAX       4

#define LAUNCH_COLS     5
#define CTX_W           228
#define CTX_ITEM_H      27

/* ── Shell state ──────────────────────────────────────────────────────────── */

static volatile int dirty = 1;
static int  cursor_visible = 1;

static u32  frame_total;
static u32  fps_value;
static u32  fps_frames;
static u32  fps_start_ms;
static u32  fps_last_second;

static int  launcher_open;
static int  control_open;
static int  ctx_open;
static int  ctx_x, ctx_y;

static char launcher_query[48];
static int  launcher_sel;

static int  desk_selected = -1;
static int  desk_last_click = -1;
static u32  desk_last_click_ms;

static char clock_text[16] = "--:--";
static char date_text[32] = "";
static u32  clock_stamp = 0xFFFFFFFFu;

static int  dock_hover = -1;

typedef struct {
    int  used;
    int  kind;
    u32  start_ms;
    char title[40];
    char msg[88];
} toast_t;

static toast_t toasts[TOAST_MAX];
static int     toast_count;

static const int desk_apps[] = {
    APP_TERMINAL, APP_FILES, APP_EDITOR, APP_DEVTOOLS,
    APP_SETTINGS, APP_HELP
};
#define DESK_APP_COUNT ((int)(sizeof(desk_apps) / sizeof(desk_apps[0])))

static const char *app_tags[APP_COUNT] = {
    "CONSOLE", "FILES", "EDITOR", "MONITOR", "SCHEDULER",
    "MATH", "KERNEL", "SYSTEM", "ABOUT", "GUIDE"
};

/* ── Small helpers ────────────────────────────────────────────────────────── */

static int point_in(int px, int py, int x, int y, int w, int h)
{
    return px >= x && py >= y && px < x + w && py < y + h;
}

static int ci_contains(const char *haystack, const char *needle)
{
    if (!needle || !*needle) return 1;
    int nlen = kstrlen(needle);
    for (const char *p = haystack; *p; p++) {
        int i = 0;
        while (i < nlen && p[i] &&
               ktolower((u8)p[i]) == ktolower((u8)needle[i]))
            i++;
        if (i == nlen) return 1;
    }
    return 0;
}

static void diamond(int cx, int cy, int r, u32 color)
{
    for (int dy = -r; dy <= r; dy++) {
        int half = r - (dy < 0 ? -dy : dy);
        gfx_fill(cx - half, cy + dy, half * 2 + 1, 1, color);
    }
}

static u32 toast_color(int kind)
{
    switch (kind) {
    case TOAST_SUCCESS: return theme.success;
    case TOAST_WARN:    return theme.warn;
    case TOAST_ERROR:   return theme.danger;
    default:            return theme.accent;
    }
}

/* ── Geometry ─────────────────────────────────────────────────────────────── */

void desktop_workspace(int *x, int *y, int *w, int *h)
{
    int top = BAR_H + 6;
    int bottom = DOCK_H + DOCK_BOTTOM + 8;
    if (x) *x = 6;
    if (y) *y = top;
    if (w) *w = max(320, gfx.width - 12);
    if (h) *h = max(200, gfx.height - top - bottom);
}

static void dock_rect(int *x, int *y, int *w, int *h)
{
    int count = APP_COUNT;
    int dw = count * DOCK_ICON + (count - 1) * DOCK_GAP + DOCK_PAD * 2;
    if (dw > gfx.width - 20) {
        /* Scale icons down on very small screens. */
        int avail = gfx.width - 40;
        int icon = max(24, (avail - (count - 1) * DOCK_GAP - DOCK_PAD * 2) / count);
        dw = count * icon + (count - 1) * DOCK_GAP + DOCK_PAD * 2;
    }
    if (x) *x = (gfx.width - dw) / 2;
    if (y) *y = gfx.height - DOCK_BOTTOM - DOCK_H;
    if (w) *w = dw;
    if (h) *h = DOCK_H;
}

static int dock_icon_size(void)
{
    int count = APP_COUNT;
    int dw = count * DOCK_ICON + (count - 1) * DOCK_GAP + DOCK_PAD * 2;
    if (dw > gfx.width - 20) {
        int avail = gfx.width - 40;
        return max(24, (avail - (count - 1) * DOCK_GAP - DOCK_PAD * 2) / count);
    }
    return DOCK_ICON;
}

static void dock_cell_rect(int index, int *x, int *y, int *w, int *h)
{
    int dx, dy, dw, dh;
    dock_rect(&dx, &dy, &dw, &dh);
    int icon = dock_icon_size();
    int cx = dx + DOCK_PAD + index * (icon + DOCK_GAP);
    if (x) *x = cx;
    if (y) *y = dy + (DOCK_H - icon) / 2;
    if (w) *w = icon;
    if (h) *h = icon;
}

static void topbar_apps_rect(int *x, int *y, int *w, int *h)
{
    if (x) *x = 88;
    if (y) *y = 6;
    if (w) *w = 66;
    if (h) *h = 22;
}

static void topbar_ctl_rect(int *x, int *y, int *w, int *h)
{
    if (x) *x = gfx.width - 142;
    if (y) *y = 5;
    if (w) *w = 132;
    if (h) *h = 24;
}

static void launcher_rect(int *x, int *y, int *w, int *h)
{
    int lw = min(780, gfx.width - 80);
    int lh = min(470, gfx.height - BAR_H - 80);
    if (x) *x = (gfx.width - lw) / 2;
    if (y) *y = BAR_H + (gfx.height - BAR_H - lh) / 2;
    if (w) *w = lw;
    if (h) *h = lh;
}

static void launcher_cell_rect(int index, int *x, int *y, int *w, int *h)
{
    int lx, ly, lw, lh;
    launcher_rect(&lx, &ly, &lw, &lh);
    int col = index % LAUNCH_COLS;
    int row = index / LAUNCH_COLS;
    int cw = (lw - 48) / LAUNCH_COLS;
    int ch = 112;
    if (x) *x = lx + 24 + col * cw;
    if (y) *y = ly + 104 + row * ch;
    if (w) *w = cw - 6;
    if (h) *h = ch - 6;
}

static void control_rect(int *x, int *y, int *w, int *h)
{
    int cw = min(340, gfx.width - 24);
    int ch = 452;
    if (x) *x = gfx.width - cw - 10;
    if (y) *y = BAR_H + 8;
    if (w) *w = cw;
    if (h) *h = ch;
}

static void cc_toggle_rect(int index, int *x, int *y, int *w, int *h)
{
    int cx, cy, cw, ch;
    control_rect(&cx, &cy, &cw, &ch);
    if (x) *x = cx + 16;
    if (y) *y = cy + 52 + index * 30;
    if (w) *w = cw - 32;
    if (h) *h = 26;
}

static void cc_accent_rect(int index, int *x, int *y, int *w, int *h)
{
    int cx, cy, cw, ch;
    control_rect(&cx, &cy, &cw, &ch);
    if (x) *x = cx + 24 + index * 58;
    if (y) *y = cy + 170;
    if (w) *w = 32;
    if (h) *h = 32;
}

static void cc_wall_rect(int index, int *x, int *y, int *w, int *h)
{
    int cx, cy, cw, ch;
    control_rect(&cx, &cy, &cw, &ch);
    int pw = (cw - 40 - 3 * 8) / 4;
    if (x) *x = cx + 20 + index * (pw + 8);
    if (y) *y = cy + 240;
    if (w) *w = pw;
    if (h) *h = 38;
}

static void cc_action_rect(int index, int *x, int *y, int *w, int *h)
{
    int cx, cy, cw, ch;
    control_rect(&cx, &cy, &cw, &ch);
    int bw = (cw - 40 - 16) / 3;
    if (x) *x = cx + 20 + index * (bw + 8);
    if (y) *y = cy + 300;
    if (w) *w = bw;
    if (h) *h = 28;
}

static void cc_power_rect(int index, int *x, int *y, int *w, int *h)
{
    int cx, cy, cw, ch;
    control_rect(&cx, &cy, &cw, &ch);
    int bw = (cw - 40 - 10) / 2;
    if (x) *x = cx + 20 + index * (bw + 10);
    if (y) *y = cy + 410;
    if (w) *w = bw;
    if (h) *h = 32;
}

static int ctx_height(void)
{
    return 6 + 5 * CTX_ITEM_H + 7 + 2 * CTX_ITEM_H + 6;
}

static void ctx_open_at(int x, int y)
{
    ctx_x = clamp(x, 4, gfx.width - CTX_W - 4);
    ctx_y = clamp(y, BAR_H + 4, gfx.height - ctx_height() - 4);
    ctx_open = 1;
    launcher_open = 0;
    control_open = 0;
    dirty = 1;
}

static void ctx_item_rect(int index, int *x, int *y, int *w, int *h)
{
    int iy = ctx_y + 6 + (index < 5 ? index * CTX_ITEM_H
                                    : 5 * CTX_ITEM_H + 7 + (index - 5) * CTX_ITEM_H);
    if (x) *x = ctx_x + 4;
    if (y) *y = iy;
    if (w) *w = CTX_W - 8;
    if (h) *h = CTX_ITEM_H;
}

/* ── Public state helpers ─────────────────────────────────────────────────── */

void desktop_invalidate(void) { dirty = 1; }
int  desktop_fps(void)        { return (int)fps_value; }
u32  desktop_frame_count(void) { return frame_total; }
void desktop_cursor_show(void) { cursor_visible = 1; dirty = 1; }
void desktop_cursor_hide(void) { cursor_visible = 0; dirty = 1; }

void toast_show(const char *title, const char *msg, int kind)
{
    if (toast_count >= TOAST_MAX) {
        for (int i = 0; i + 1 < TOAST_MAX; i++)
            toasts[i] = toasts[i + 1];
        toast_count = TOAST_MAX - 1;
    }

    toast_t *t = &toasts[toast_count++];
    memset(t, 0, sizeof(*t));
    t->used = 1;
    t->kind = kind;
    t->start_ms = timer_ms();
    kstrncpy(t->title, title ? title : "NOVA", sizeof(t->title));
    kstrncpy(t->msg, msg ? msg : "", sizeof(t->msg));
    dirty = 1;
}

/* ── Clock ────────────────────────────────────────────────────────────────── */

static void update_clock(void)
{
    rtc_time_t t;
    rtc_read(&t);

    char tbuf[16];
    if (g_settings.clock_seconds)
        rtc_format_time_sec(&t, tbuf, sizeof(tbuf));
    else
        rtc_format_time(&t, tbuf, sizeof(tbuf));

    u32 stamp = (u32)t.hour * 3600u + (u32)t.minute * 60u +
                (g_settings.clock_seconds ? (u32)t.second : 0u);
    if (stamp == clock_stamp) return;

    clock_stamp = stamp;
    kstrncpy(clock_text, tbuf, sizeof(clock_text));
    rtc_format_date(&t, date_text, sizeof(date_text));
    dirty = 1;
}

/* ── Launcher model ───────────────────────────────────────────────────────── */

static int launcher_matches(int app, const char *query)
{
    const app_info_t *info = app_info((app_id_t)app);
    if (!query || !*query) return 1;
    return ci_contains(info->name, query) ||
           ci_contains(info->short_name, query) ||
           ci_contains(info->description, query);
}

static int launcher_collect(int *out)
{
    int n = 0;
    for (int i = 0; i < APP_COUNT; i++) {
        if (launcher_matches(i, launcher_query)) {
            out[n++] = i;
        }
    }
    return n;
}

static void launcher_launch(int app)
{
    launcher_open = 0;
    control_open = 0;
    ctx_open = 0;
    app_open((app_id_t)app);
    dirty = 1;
}

/* ── Shell drawing ────────────────────────────────────────────────────────── */

static void draw_wallpaper_mark(void)
{
    int cx = gfx.width / 2;
    int cy = BAR_H + (gfx.height - BAR_H - DOCK_H) / 2;
    gfx_text_alpha(cx - gfx_text_width_scale("NOVA", 4) / 2, cy - 44,
                   "NOVA", 0xFFFFFF, 18);
    gfx_text_alpha(cx - gfx_text_width("PROGRAMMER OPERATING SYSTEM") / 2,
                   cy + 26, "PROGRAMMER OPERATING SYSTEM", 0xFFFFFF, 30);
}

static void draw_desktop_icons(void)
{
    mouse_state_t *m = mouse_get();
    int mx = m ? m->x : -1;
    int my = m ? m->y : -1;

    for (int i = 0; i < DESK_APP_COUNT; i++) {
        int x = DESK_LEFT;
        int y = DESK_TOP + i * DESK_CELL_H;
        int app = desk_apps[i];
        int hot = point_in(mx, my, x, y, DESK_CELL_W, DESK_CELL_H);
        int selected = (desk_selected == i);

        if (selected || hot) {
            gfx_round_rect_blend(x + 2, y + 2, DESK_CELL_W - 4,
                                 DESK_CELL_H - 6, 12,
                                 selected ? theme.accent : 0xFFFFFF,
                                 selected ? 30 : 16);
            if (selected)
                gfx_round_outline(x + 2, y + 2, DESK_CELL_W - 4,
                                  DESK_CELL_H - 6, 12,
                                  gfx_lerp(theme.border, theme.accent, 150));
        }

        int ix = x + (DESK_CELL_W - DESK_ICON) / 2;
        int iy = y + 8;
        app_icon_draw((app_id_t)app, ix, iy, DESK_ICON, selected);

        const char *name = app_info((app_id_t)app)->name;
        int nw = gfx_text_width(name);
        gfx_text(x + (DESK_CELL_W - nw) / 2, iy + DESK_ICON + 12,
                 name, selected ? theme.text : theme.text_dim);
    }
}

static void draw_topbar(void)
{
    int w = gfx.width;
    mouse_state_t *m = mouse_get();
    int mx = m ? m->x : -1;
    int my = m ? m->y : -1;

    gfx_fill_blend(0, 0, w, BAR_H, theme.panel, 238);
    gfx_fill(0, BAR_H - 1, w, 1, theme.panel_border);

    /* Logo */
    diamond(18, BAR_H / 2, 8, theme.accent);
    diamond(18, BAR_H / 2, 4, 0x06101F);
    gfx_text(32, 9, "NOVA", theme.text);

    /* Apps button */
    int ax, ay, aw, ah;
    topbar_apps_rect(&ax, &ay, &aw, &ah);
    int apps_hot = point_in(mx, my, ax, ay, aw, ah);
    gfx_round_rect(ax, ay, aw, ah, 7,
                   apps_hot || launcher_open ? theme.panel2 : 0);
    if (apps_hot || launcher_open)
        gfx_round_outline(ax, ay, aw, ah, 7, theme.border);
    gfx_text(ax + 12, ay + 3, "Apps", launcher_open ? theme.accent : theme.text_dim);

    /* Focused window / session label */
    window_t *focus = wm_focused();
    gfx_set_clip(168, 4, max(10, gfx.width / 2 - 190), BAR_H - 8);
    gfx_text(168, 9, focus ? focus->title : "Desktop", theme.text_dim);
    gfx_reset_clip();

    /* Centered date and clock. */
    char center[64];
    ksnprintf(center, sizeof(center), "%s    %s", date_text, clock_text);
    gfx_set_clip(gfx.width / 2 - 180, 0, 360, BAR_H);
    gfx_text_center(gfx.width / 2, 9, center, theme.text);
    gfx_reset_clip();

    /* Right side status. */
    int cx, cy, cw, ch;
    topbar_ctl_rect(&cx, &cy, &cw, &ch);
    int ctl_hot = point_in(mx, my, cx, cy, cw, ch);

    char memtext[32];
    u32 used = mem_used();
    u32 total = max(1u, mem_total());
    ksnprintf(memtext, sizeof(memtext), "MEM %u%%", (used * 100u) / total);
    gfx_text_right(cx - 16, 9, memtext,
                   (used * 100u) / total > 80 ? theme.warn : theme.text_faint);

    if (g_settings.show_fps) {
        char fps[32];
        ksnprintf(fps, sizeof(fps), "FPS %d", (int)fps_value);
        gfx_text_right(cx - 92, 9, fps, theme.text_faint);
    }

    gfx_round_rect(cx, cy, cw, ch, 9,
                   (ctl_hot || control_open) ? theme.panel2 : 0x0E1728);
    gfx_round_outline(cx, cy, cw, ch, 9,
                      control_open ? theme.accent : theme.border);
    gfx_circle_fill(cx + 14, cy + ch / 2, 4,
                    control_open ? theme.accent : theme.success);
    gfx_text(cx + 26, cy + 4, "Control", control_open ? theme.text : theme.text_dim);
    gfx_text_right(cx + cw - 10, cy + 4, "..", theme.text_faint);
}

static void draw_dock(void)
{
    int dx, dy, dw, dh;
    dock_rect(&dx, &dy, &dw, &dh);

    mouse_state_t *m = mouse_get();
    int mx = m ? m->x : -1;
    int my = m ? m->y : -1;

    gfx_shadow(dx, dy, dw, dh, 16, 12, 90);
    gfx_round_rect(dx, dy, dw, dh, 16, theme.panel);
    gfx_round_rect_blend(dx + 1, dy + 1, dw - 2, dh / 2, 15, 0xFFFFFF, 6);
    gfx_round_outline(dx, dy, dw, dh, 16, theme.panel_border);

    int icon = dock_icon_size();
    int hover = -1;
    for (int i = 0; i < APP_COUNT; i++) {
        int cx, cy, cw, ch;
        dock_cell_rect(i, &cx, &cy, &cw, &ch);
        if (point_in(mx, my, cx - DOCK_GAP / 2, cy - 6, cw + DOCK_GAP, ch + 12))
            hover = i;
    }
    dock_hover = hover;

    for (int i = 0; i < APP_COUNT; i++) {
        int cx, cy, cw, ch;
        dock_cell_rect(i, &cx, &cy, &cw, &ch);

        int hot = (hover == i);
        int size = icon;
        int iy = cy;

        if (hot && g_settings.dock_magnify) {
            size = icon + 8;
            iy = cy - 6;
        }

        int ix = cx + (icon - size) / 2;
        window_t *win = wm_find(i);
        app_icon_draw((app_id_t)i, ix, iy, size,
                      win && win->focused);

        if (win && win->used) {
            u32 dot = win->minimized ? theme.text_faint :
                      (win->focused ? theme.accent : theme.text_dim);
            gfx_circle_fill(cx + icon / 2, dy + dh - 7, 2, dot);
        }
    }

    if (hover >= 0) {
        const char *name = app_info((app_id_t)hover)->name;
        int tw = gfx_text_width(name) + 20;
        int tx = dx + DOCK_PAD + hover * (icon + DOCK_GAP) +
                 (icon - tw) / 2;
        if (tx < 6) tx = 6;
        if (tx + tw > gfx.width - 6) tx = gfx.width - tw - 6;
        gfx_round_rect(tx, dy - 34, tw, 26, 8, 0x020617);
        gfx_round_outline(tx, dy - 34, tw, 26, 8, theme.border);
        gfx_text(tx + 10, dy - 28, name, theme.text);
    }
}

static void draw_launcher(void)
{
    if (!launcher_open) return;

    gfx_fill_blend(0, 0, gfx.width, gfx.height, 0x02040A, 170);

    int lx, ly, lw, lh;
    launcher_rect(&lx, &ly, &lw, &lh);

    gfx_shadow(lx, ly, lw, lh, 18, 20, 140);
    gfx_round_rect(lx, ly, lw, lh, 18, theme.panel);
    gfx_round_outline(lx, ly, lw, lh, 18, theme.panel_border);

    gfx_text(lx + 24, ly + 18, "Applications", theme.text);
    gfx_text_right(lx + lw - 24, ly + 18, "NOVA shell", theme.text_faint);

    ui_text_input(lx + 24, ly + 46, lw - 48, 32,
                  launcher_query, kstrlen(launcher_query), 1,
                  "Search applications...");

    int matches[APP_COUNT];
    int count = launcher_collect(matches);
    if (launcher_sel >= count) launcher_sel = max(0, count - 1);

    mouse_state_t *m = mouse_get();
    int mx = m ? m->x : -1;
    int my = m ? m->y : -1;

    for (int i = 0; i < count; i++) {
        int cx, cy, cw, ch;
        launcher_cell_rect(i, &cx, &cy, &cw, &ch);
        int hot = point_in(mx, my, cx, cy, cw, ch);
        int selected = (i == launcher_sel);

        if (hot || selected) {
            gfx_round_rect(cx, cy, cw, ch, 12,
                           selected ? theme.panel2 : gfx_lerp(theme.panel, 0xFFFFFF, 10));
            gfx_round_outline(cx, cy, cw, ch, 12,
                              selected ? theme.accent : theme.border);
        }

        int app = matches[i];
        int icon_x = cx + (cw - 40) / 2;
        app_icon_draw((app_id_t)app, icon_x, cy + 14, 40, selected);

        const char *name = app_info((app_id_t)app)->name;
        int nw = gfx_text_width(name);
        gfx_text(cx + (cw - nw) / 2, cy + 62, name,
                 selected ? theme.text : theme.text_dim);
        const char *tag = app_tags[app];
        gfx_text(cx + (cw - gfx_text_width(tag)) / 2, cy + 84,
                 tag, selected ? theme.accent : theme.text_faint);
    }

    if (count == 0) {
        gfx_text_center(lx + lw / 2, ly + 190,
                        "No applications match your search.", theme.text_faint);
    }

    gfx_text(lx + 24, ly + lh - 28,
             "Type to filter   -   Arrow keys to navigate   -   Enter to launch   -   Esc to close",
             theme.text_faint);
}

static void draw_control_center(void)
{
    if (!control_open) return;

    int cx, cy, cw, ch;
    control_rect(&cx, &cy, &cw, &ch);
    mouse_state_t *m = mouse_get();
    int mx = m ? m->x : -1;
    int my = m ? m->y : -1;

    gfx_shadow(cx, cy, cw, ch, 16, 16, 120);
    gfx_round_rect(cx, cy, cw, ch, 16, theme.panel);
    gfx_round_outline(cx, cy, cw, ch, 16, theme.panel_border);

    gfx_text(cx + 18, cy + 14, "Control Center", theme.text);
    gfx_text_right(cx + cw - 18, cy + 14, "NOVA", theme.text_faint);
    gfx_fill(cx + 16, cy + 38, cw - 32, 1, theme.border);

    static const char *toggles[3] = {
        "Interface animations", "Dock magnification", "Clock seconds"
    };
    int values[3] = {
        g_settings.animations, g_settings.dock_magnify, g_settings.clock_seconds
    };

    for (int i = 0; i < 3; i++) {
        int x, y, w, h;
        cc_toggle_rect(i, &x, &y, &w, &h);
        int hot = point_in(mx, my, x, y, w, h);
        if (hot) gfx_round_rect(x, y, w, h, 7, theme.panel2);
        gfx_text(x + 6, y + 5, toggles[i], theme.text_dim);
        ui_toggle(x + w - 46, y + 3, 40, 20, values[i], NULL);
    }

    gfx_text(cx + 18, cy + 148, "Accent colour", theme.text_faint);
    for (int i = 0; i < 5; i++) {
        int x, y, w, h;
        cc_accent_rect(i, &x, &y, &w, &h);
        int selected = (g_settings.accent == i);
        int hot = point_in(mx, my, x - 4, y - 4, w + 8, h + 8);
        if (selected)
            gfx_circle_ring(x + w / 2, y + h / 2, w / 2 + 5, 2, accent_colors[i]);
        gfx_circle_fill(x + w / 2, y + h / 2, w / 2,
                        hot ? gfx_lerp(accent_colors[i], 0xFFFFFF, 60)
                            : accent_colors[i]);
    }

    gfx_text(cx + 18, cy + 218, "Wallpaper", theme.text_faint);
    for (int i = 0; i < 4; i++) {
        int x, y, w, h;
        cc_wall_rect(i, &x, &y, &w, &h);
        int selected = (g_settings.wallpaper == i);
        int hot = point_in(mx, my, x, y, w, h);
        gfx_round_rect(x, y, w, h, 7, gfx_lerp(theme.bg0, theme.bg1, 80));
        gfx_round_rect_blend(x + 2, y + 2, w - 4, h / 2, 6,
                             theme.accent, 22 + i * 10);
        gfx_round_outline(x, y, w, h, 7,
                          selected ? theme.accent :
                          (hot ? theme.text_faint : theme.border));
        if (selected)
            gfx_circle_fill(x + w - 7, y + 7, 3, theme.accent);
    }

    static const char *actions[3] = { "Terminal", "Settings", "Help" };
    for (int i = 0; i < 3; i++) {
        int x, y, w, h;
        cc_action_rect(i, &x, &y, &w, &h);
        int hot = point_in(mx, my, x, y, w, h);
        ui_button(x, y, w, h, actions[i], hot, i == 0);
    }

    ui_card(cx + 16, cy + 338, cw - 32, 64, theme.win2, theme.border);
    char line[64];
    ksnprintf(line, sizeof(line), "Heap   %u KiB / %u KiB",
              mem_used() / 1024u, mem_total() / 1024u);
    gfx_text(cx + 30, cy + 350, line, theme.text_dim);
    ksnprintf(line, sizeof(line), "Windows %d      Processes %d",
              wm_count(), proc_active_count());
    gfx_text(cx + 30, cy + 370, line, theme.text_faint);

    for (int i = 0; i < 2; i++) {
        int x, y, w, h;
        cc_power_rect(i, &x, &y, &w, &h);
        int hot = point_in(mx, my, x, y, w, h);
        u32 col = i == 0 ? theme.warn : theme.danger;
        gfx_round_rect(x, y, w, h, 8, hot ? gfx_lerp(col, 0x000000, 40) : theme.panel2);
        gfx_round_outline(x, y, w, h, 8, hot ? col : theme.border);
        gfx_text(x + (w - gfx_text_width(i == 0 ? "Reboot" : "Shut down")) / 2,
                 y + 8, i == 0 ? "Reboot" : "Shut down", hot ? theme.text : theme.text_dim);
    }
}

static void draw_context_menu(void)
{
    if (!ctx_open) return;

    int w = CTX_W;
    int h = ctx_height();
    mouse_state_t *m = mouse_get();
    int mx = m ? m->x : -1;
    int my = m ? m->y : -1;

    static const char *labels[7] = {
        "Open Terminal", "Open Files", "Open Settings", "Open Help",
        "System Monitor", "Reboot", "Shut down"
    };

    gfx_shadow(ctx_x, ctx_y, w, h, 10, 12, 120);
    gfx_round_rect(ctx_x, ctx_y, w, h, 10, theme.panel);
    gfx_round_outline(ctx_x, ctx_y, w, h, 10, theme.panel_border);

    for (int i = 0; i < 7; i++) {
        int x, y, iw, ih;
        ctx_item_rect(i, &x, &y, &iw, &ih);
        int hot = point_in(mx, my, x, y, iw, ih);
        if (hot)
            gfx_round_rect(x, y, iw, ih, 6, theme.panel2);
        gfx_text(x + 12, y + 5, labels[i],
                 (i >= 5 && hot) ? theme.danger :
                 (hot ? theme.text : theme.text_dim));
    }

    int div_y = ctx_y + 6 + 5 * CTX_ITEM_H + 3;
    gfx_fill(ctx_x + 10, div_y, w - 20, 1, theme.border);
}

static void draw_toasts(void)
{
    int dx, dy, dw, dh;
    dock_rect(&dx, &dy, &dw, &dh);
    u32 now = timer_ms();

    int shown = 0;
    for (int i = toast_count - 1; i >= 0; i--) {
        toast_t *t = &toasts[i];
        if (!t->used) continue;
        if (now - t->start_ms > TOAST_LIFE_MS) {
            t->used = 0;
            continue;
        }

        int x = gfx.width - TOAST_W - 16;
        int y = dy - 18 - TOAST_H - shown * (TOAST_H + TOAST_GAP);
        if (y < BAR_H + 8) break;

        if (g_settings.animations) {
            u32 age = now - t->start_ms;
            int slide = age < 180 ? (int)((180 - age) / 8) : 0;
            x += slide;
        }

        u32 accent = toast_color(t->kind);

        gfx_shadow(x, y, TOAST_W, TOAST_H, 10, 10, 100);
        gfx_round_rect(x, y, TOAST_W, TOAST_H, 10, theme.panel);
        gfx_round_outline(x, y, TOAST_W, TOAST_H, 10, theme.panel_border);
        gfx_fill(x + 1, y + 8, 3, TOAST_H - 16, accent);
        gfx_circle_fill(x + 20, y + 20, 6, accent);
        gfx_text(x + 34, y + 9, t->title, theme.text);
        gfx_text(x + 34, y + 29, t->msg, theme.text_dim);
        shown++;
    }

    /* Compact the queue when the oldest entries expired. */
    if (shown == 0 && toast_count > 0) {
        toast_count = 0;
    }
}

static void draw_fps_badge(void)
{
    if (!g_settings.show_fps) return;

    char buf[48];
    ksnprintf(buf, sizeof(buf), "%d fps  |  %u frames",
              (int)fps_value, frame_total);
    int w = gfx_text_width(buf) + 16;
    int x = gfx.width - w - 10;
    int y = gfx.height - 26;
    gfx_round_rect_blend(x, y, w, 18, 8, 0x020617, 190);
    gfx_text(x + 8, y + 1, buf, theme.text_faint);
}

static void draw_cursor(void)
{
    if (!cursor_visible) return;

    mouse_state_t *m = mouse_get();
    if (!m) return;

    int mx = m->x;
    int my = m->y;

    /* White outline pass. */
    for (int y = 0; y <= 10; y++) {
        for (int x = 0; x <= y + 1; x++)
            gfx_pixel(mx + x, my + y, 0xFFFFFF);
    }
    for (int y = 9; y <= 18; y++) {
        for (int x = 4; x <= 7; x++)
            gfx_pixel(mx + x, my + y, 0xFFFFFF);
    }

    /* Dark fill pass. */
    for (int y = 1; y <= 10; y++) {
        for (int x = 1; x <= y; x++)
            gfx_pixel(mx + x, my + y, 0x0A0F1A);
    }
    for (int y = 10; y <= 17; y++) {
        for (int x = 5; x <= 6; x++)
            gfx_pixel(mx + x, my + y, 0x0A0F1A);
    }
}

static void draw_frame(void)
{
    gfx_blit_wall_to_back();

    draw_wallpaper_mark();
    draw_desktop_icons();

    wm_draw_all();

    draw_topbar();
    draw_dock();

    if (launcher_open) draw_launcher();
    if (control_open) draw_control_center();
    if (ctx_open) draw_context_menu();

    draw_toasts();
    draw_fps_badge();

    gfx_reset_clip();
    draw_cursor();

    frame_total++;
    fps_frames++;
}

/* ── Input: left button routing ───────────────────────────────────────────── */

static void launch_or_focus(int app)
{
    window_t *w = wm_find(app);
    if (w) {
        if (w->minimized) wm_focus(w);
        else if (w->focused) wm_minimize(w);
        else wm_focus(w);
        dirty = 1;
        return;
    }
    app_open((app_id_t)app);
    dirty = 1;
}

static int handle_launcher_click(int x, int y)
{
    int lx, ly, lw, lh;
    launcher_rect(&lx, &ly, &lw, &lh);

    if (!point_in(x, y, lx, ly, lw, lh)) {
        launcher_open = 0;
        dirty = 1;
        return 1;
    }

    int matches[APP_COUNT];
    int count = launcher_collect(matches);

    for (int i = 0; i < count; i++) {
        int cx, cy, cw, ch;
        launcher_cell_rect(i, &cx, &cy, &cw, &ch);
        if (point_in(x, y, cx, cy, cw, ch)) {
            launcher_launch(matches[i]);
            return 1;
        }
    }

    return 1;
}

static int handle_control_click(int x, int y)
{
    int cx, cy, cw, ch;
    control_rect(&cx, &cy, &cw, &ch);

    if (!point_in(x, y, cx, cy, cw, ch)) {
        control_open = 0;
        dirty = 1;
        return 1;
    }

    for (int i = 0; i < 3; i++) {
        int rx, ry, rw, rh;
        cc_toggle_rect(i, &rx, &ry, &rw, &rh);
        if (point_in(x, y, rx, ry, rw, rh)) {
            if (i == 0) g_settings.animations = !g_settings.animations;
            else if (i == 1) g_settings.dock_magnify = !g_settings.dock_magnify;
            else g_settings.clock_seconds = !g_settings.clock_seconds;
            settings_save();
            clock_stamp = 0xFFFFFFFFu;
            update_clock();
            dirty = 1;
            return 1;
        }
    }

    for (int i = 0; i < 5; i++) {
        int rx, ry, rw, rh;
        cc_accent_rect(i, &rx, &ry, &rw, &rh);
        if (point_in(x, y, rx - 4, ry - 4, rw + 8, rh + 8)) {
            g_settings.accent = i;
            settings_apply();
            settings_save();
            dirty = 1;
            return 1;
        }
    }

    for (int i = 0; i < 4; i++) {
        int rx, ry, rw, rh;
        cc_wall_rect(i, &rx, &ry, &rw, &rh);
        if (point_in(x, y, rx, ry, rw, rh)) {
            g_settings.wallpaper = i;
            settings_apply();
            settings_save();
            dirty = 1;
            return 1;
        }
    }

    for (int i = 0; i < 3; i++) {
        int rx, ry, rw, rh;
        cc_action_rect(i, &rx, &ry, &rw, &rh);
        if (point_in(x, y, rx, ry, rw, rh)) {
            control_open = 0;
            if (i == 0) app_open(APP_TERMINAL);
            else if (i == 1) app_open(APP_SETTINGS);
            else app_open(APP_HELP);
            dirty = 1;
            return 1;
        }
    }

    for (int i = 0; i < 2; i++) {
        int rx, ry, rw, rh;
        cc_power_rect(i, &rx, &ry, &rw, &rh);
        if (point_in(x, y, rx, ry, rw, rh)) {
            control_open = 0;
            if (i == 0) app_power_reboot();
            else app_power_halt();
            return 1;
        }
    }

    return 1;
}

static int handle_context_click(int x, int y)
{
    if (!point_in(x, y, ctx_x, ctx_y, CTX_W, ctx_height())) {
        ctx_open = 0;
        dirty = 1;
        return 1;
    }

    for (int i = 0; i < 7; i++) {
        int ix, iy, iw, ih;
        ctx_item_rect(i, &ix, &iy, &iw, &ih);
        if (point_in(x, y, ix, iy, iw, ih)) {
            ctx_open = 0;
            switch (i) {
            case 0: app_open(APP_TERMINAL); break;
            case 1: app_open(APP_FILES); break;
            case 2: app_open(APP_SETTINGS); break;
            case 3: app_open(APP_HELP); break;
            case 4: app_open(APP_MONITOR); break;
            case 5: app_power_reboot(); break;
            case 6: app_power_halt(); break;
            default: break;
            }
            dirty = 1;
            return 1;
        }
    }

    return 1;
}

static int handle_desktop_icon_click(int x, int y)
{
    for (int i = 0; i < DESK_APP_COUNT; i++) {
        int ix = DESK_LEFT;
        int iy = DESK_TOP + i * DESK_CELL_H;
        if (!point_in(x, y, ix, iy, DESK_CELL_W, DESK_CELL_H)) continue;

        u32 now = timer_ms();
        if (desk_last_click == i && now - desk_last_click_ms < 420) {
            desk_last_click = -1;
            app_open((app_id_t)desk_apps[i]);
        } else {
            desk_selected = i;
            desk_last_click = i;
            desk_last_click_ms = now;
        }
        dirty = 1;
        return 1;
    }
    return 0;
}

static void handle_left_down(int x, int y)
{
    if (launcher_open) { handle_launcher_click(x, y); return; }
    if (control_open)  { handle_control_click(x, y); return; }
    if (ctx_open)      { handle_context_click(x, y); return; }

    int ax, ay, aw, ah;
    topbar_apps_rect(&ax, &ay, &aw, &ah);
    int cx, cy, cw, ch;
    topbar_ctl_rect(&cx, &cy, &cw, &ch);

    if (y < BAR_H) {
        if (point_in(x, y, ax, ay, aw, ah) || x < 80) {
            launcher_open = !launcher_open;
            control_open = 0;
            ctx_open = 0;
            dirty = 1;
            return;
        }
        if (point_in(x, y, cx, cy, cw, ch)) {
            control_open = !control_open;
            launcher_open = 0;
            ctx_open = 0;
            dirty = 1;
            return;
        }
        return;
    }

    int dx, dy, dw, dh;
    dock_rect(&dx, &dy, &dw, &dh);
    if (point_in(x, y, dx, dy, dw, dh)) {
        for (int i = 0; i < APP_COUNT; i++) {
            int ix, iy, iw, ih;
            dock_cell_rect(i, &ix, &iy, &iw, &ih);
            if (point_in(x, y, ix - DOCK_GAP / 2, iy - 8,
                         iw + DOCK_GAP, ih + 16)) {
                if (i == APP_SETTINGS && wm_find(APP_SETTINGS))
                    wm_focus(wm_find(APP_SETTINGS));
                else
                    launch_or_focus(i);
                return;
            }
        }
        return;
    }

    if (wm_mouse_down(x, y, MOUSE_BTN_LEFT))
        return;

    if (handle_desktop_icon_click(x, y))
        return;

    desk_selected = -1;
    dirty = 1;
}

static void handle_right_down(int x, int y)
{
    if (launcher_open || control_open) {
        launcher_open = 0;
        control_open = 0;
        dirty = 1;
        return;
    }
    if (ctx_open) {
        ctx_open = 0;
        dirty = 1;
        return;
    }
    if (y > BAR_H && !wm_hit_window(x, y, NULL, NULL)) {
        ctx_open_at(x, y);
    }
}

/* ── Input: keyboard ──────────────────────────────────────────────────────── */

static void cycle_windows(void)
{
    int count = wm_count();
    if (count <= 0) return;

    window_t *focused = wm_focused();
    int start = 0;
    for (int i = 0; i < count; i++) {
        if (wm_at(i) == focused) { start = i; break; }
    }
    for (int step = 1; step <= count; step++) {
        window_t *w = wm_at((start + step) % count);
        if (w && w->used && !w->minimized) {
            wm_focus(w);
            return;
        }
    }
}

static void minimize_all(void)
{
    for (int i = wm_count() - 1; i >= 0; i--) {
        window_t *w = wm_at(i);
        if (w && w->used) wm_minimize(w);
    }
    dirty = 1;
}

static void handle_key(const key_event_t *ev)
{
    /* Super chords must be recognised even while the launcher is open:
     * pressing Super opens it, then the second key of the chord arrives with
     * MOD_SUPER set. */
    if ((ev->mods & MOD_SUPER) && ev->ch) {
        launcher_open = 0;
        control_open = 0;
        ctx_open = 0;
        switch (ev->ch) {
        case 'e': case 'E': app_open(APP_FILES);    dirty = 1; return;
        case 't': case 'T': app_open(APP_TERMINAL); dirty = 1; return;
        case 's': case 'S': app_open(APP_SETTINGS); dirty = 1; return;
        case 'h': case 'H': app_open(APP_HELP);     dirty = 1; return;
        case 'c': case 'C': control_open = 1;        dirty = 1; return;
        case 'd': case 'D': minimize_all();          return;
        default: break;
        }
    }

    /* Launcher captures typing. */
    if (launcher_open) {
        if (ev->key == KEY_ESC) {
            launcher_open = 0;
            dirty = 1;
            return;
        }
        if (ev->key == KEY_ENTER) {
            int matches[APP_COUNT];
            int count = launcher_collect(matches);
            if (count > 0) {
                int sel = clamp(launcher_sel, 0, count - 1);
                launcher_launch(matches[sel]);
            }
            return;
        }
        if (ev->key == KEY_BACKSPACE) {
            int len = kstrlen(launcher_query);
            if (len > 0) launcher_query[len - 1] = '\0';
            launcher_sel = 0;
            dirty = 1;
            return;
        }
        if (ev->key == KEY_LEFT || ev->key == KEY_UP) {
            if (launcher_sel > 0) launcher_sel--;
            dirty = 1;
            return;
        }
        if (ev->key == KEY_RIGHT || ev->key == KEY_DOWN) {
            int matches[APP_COUNT];
            int count = launcher_collect(matches);
            if (launcher_sel + 1 < count) launcher_sel++;
            dirty = 1;
            return;
        }
        if (ev->ch >= 32 && ev->ch < 127) {
            int len = kstrlen(launcher_query);
            if (len < (int)sizeof(launcher_query) - 1) {
                launcher_query[len] = (char)ev->ch;
                launcher_query[len + 1] = '\0';
                launcher_sel = 0;
                dirty = 1;
            }
            return;
        }
        return;
    }

    if (ev->key == KEY_SUPER) {
        launcher_open = !launcher_open;
        control_open = 0;
        ctx_open = 0;
        if (launcher_open) {
            launcher_query[0] = '\0';
            launcher_sel = 0;
        }
        dirty = 1;
        return;
    }

    if (ev->key == KEY_ESC) {
        if (control_open || ctx_open) {
            control_open = 0;
            ctx_open = 0;
            dirty = 1;
            return;
        }
        wm_key(ev);
        return;
    }

    if (ev->key == KEY_F1) {
        app_open(APP_HELP);
        return;
    }

    if (ev->mods & MOD_ALT && ev->key == KEY_TAB) {
        cycle_windows();
        return;
    }

    if (ev->mods & MOD_CTRL && ev->mods & MOD_ALT) {
        if (ev->ch == 't' || ev->ch == 'T') { app_open(APP_TERMINAL); return; }
        if (ev->ch == 'e' || ev->ch == 'E') { app_open(APP_FILES); return; }
        if (ev->ch == 'd' || ev->ch == 'D') { minimize_all(); return; }
    }

    wm_key(ev);
}

/* ── Main loop ────────────────────────────────────────────────────────────── */

void desktop_init(void)
{
    dirty = 1;
    cursor_visible = 1;
    frame_total = 0;
    fps_value = 0;
    fps_frames = 0;
    fps_start_ms = timer_ms();
    fps_last_second = timer_seconds();

    launcher_open = control_open = ctx_open = 0;
    launcher_query[0] = '\0';
    launcher_sel = 0;
    desk_selected = -1;
    desk_last_click = -1;
    desk_last_click_ms = 0;
    dock_hover = -1;
    toast_count = 0;

    update_clock();
    klog(KLOG_LEVEL_OK, "desktop compositor ready (%dx%d)", gfx.width, gfx.height);
}

void desktop_run(void)
{
    int last_buttons = 0;
    u32 last_tick = timer_ticks();
    u32 last_sec = timer_seconds();
    u32 last_sched = last_tick;
    u32 last_fps_sec = last_sec;

    for (;;) {
        mouse_state_t *m = mouse_get();
        if (!m) { cpu_hlt(); continue; }

        /* ── Keyboard ─────────────────────────────────────────────────────── */
        key_event_t ev;
        while (keyboard_poll(&ev))
            handle_key(&ev);

        /* ── Mouse buttons ────────────────────────────────────────────────── */
        int buttons = m->buttons;
        int pressed = buttons & ~last_buttons;
        int released = last_buttons & ~buttons;

        if (m->moved || pressed || released) {
            if (launcher_open || control_open || ctx_open) {
                dirty = 1;
            } else {
                wm_mouse_move(m->x, m->y, buttons);
            }
            m->moved = 0;
        }

        if (pressed & MOUSE_BTN_LEFT)
            handle_left_down(m->x, m->y);
        if (pressed & MOUSE_BTN_RIGHT)
            handle_right_down(m->x, m->y);

        if (released & MOUSE_BTN_LEFT) {
            if (!launcher_open && !control_open && !ctx_open)
                wm_mouse_up(m->x, m->y, MOUSE_BTN_LEFT);
        }

        if (m->wheel_delta != 0) {
            int delta = m->wheel_delta;
            m->wheel_delta = 0;
            if (!launcher_open && !control_open && !ctx_open)
                wm_wheel(delta, m->x, m->y);
        }
        last_buttons = buttons;

        /* ── Timer driven work ────────────────────────────────────────────── */
        u32 now_tick = timer_ticks();
        if (now_tick != last_tick) {
            apps_on_tick(now_tick - last_tick);
            last_tick = now_tick;
        }

        u32 now_sec = timer_seconds();
        if (now_sec != last_sec) {
            last_sec = now_sec;
            apps_on_second();
            update_clock();

            if (now_sec != fps_last_second) {
                fps_value = fps_frames;
                fps_frames = 0;
                fps_last_second = now_sec;
                if (g_settings.show_fps) dirty = 1;
            }
        }

        /* Simulated CPU scheduling for the Process Manager demo. */
        if (now_tick - last_sched >= 20) {
            last_sched = now_tick;
            if (apps_auto_tick_due())
                sched_tick();
        }

        /* Clock changes and toast animation keep the frame warm. */
        update_clock();
        if (toast_count > 0) dirty = 1;

        /* ── Render ───────────────────────────────────────────────────────── */
        if (dirty) {
            dirty = 0;
            draw_frame();
            gfx_present();
        } else {
            cpu_hlt();
        }

        /* FPS sampling drift correction on idle systems. */
        (void)last_fps_sec;
    }
}
