/* ============================================================================
 * src/gui/window.c  -  Window manager with focus, dragging and resizing
 *
 * Windows are stored in a fixed table.  A separate z-order array decides what
 * is drawn on top and which window receives input.  Each frame the compositor
 * asks us to draw the visible windows; the title bar, shadow, border and
 * traffic-light buttons are rendered here, then the app's draw callback fills
 * the content rectangle under a clip region.
 * ============================================================================ */

#include "kernel.h"
#include "window.h"
#include "desktop.h"
#include "apps.h"
#include "theme.h"
#include "gfx.h"
#include "mouse.h"

#define WM_TITLE_BTN   22
#define WM_TITLE_GAP   5
#define WM_RESIZE_GRIP 6

static window_t  windows[WM_MAX_WINDOWS];
static int       order[WM_MAX_WINDOWS];   /* bottom .. top */
static int       order_count;

static window_t *drag_win;
static int       drag_mode;               /* 0=none 1=move 2=right 3=bottom 4=br 5=left */
static int       drag_off_x, drag_off_y;
static int       drag_mx, drag_my;
static int       drag_x, drag_y, drag_w, drag_h;

static int       last_mx = -1, last_my = -1;
static int       last_buttons;

/* ── Geometry helpers ─────────────────────────────────────────────────────── */

static int order_index(window_t *w)
{
    for (int i = 0; i < order_count; i++)
        if (&windows[order[i]] == w) return i;
    return -1;
}

static void order_remove_at(int index)
{
    if (index < 0 || index >= order_count) return;
    for (int i = index; i + 1 < order_count; i++)
        order[i] = order[i + 1];
    order_count--;
}

static void order_raise(window_t *w)
{
    int idx = order_index(w);
    if (idx < 0) return;
    order_remove_at(idx);
    if (order_count < WM_MAX_WINDOWS)
        order[order_count++] = (int)(w - windows);
}

static int point_in(int px, int py, int x, int y, int w, int h)
{
    return px >= x && py >= y && px < x + w && py < y + h;
}

static void title_buttons(window_t *w, int *minx, int *maxx, int *closex, int *by)
{
    int bw = WM_TITLE_BTN;
    int total = bw * 3 + WM_TITLE_GAP * 2;
    *by     = w->y + (WM_TITLE_H - bw) / 2;
    *minx   = w->x + w->w - 10 - total;
    *maxx   = *minx + bw + WM_TITLE_GAP;
    *closex = *maxx + bw + WM_TITLE_GAP;
}

static window_t *window_at(int x, int y, int content_only, int *lx, int *ly)
{
    for (int i = order_count - 1; i >= 0; i--) {
        window_t *w = &windows[order[i]];
        if (!w->used || !w->visible || w->minimized) continue;
        if (content_only) {
            int cx, cy, cw, ch;
            wm_content_rect(w, &cx, &cy, &cw, &ch);
            if (point_in(x, y, cx, cy, cw, ch)) {
                if (lx) *lx = x - cx;
                if (ly) *ly = y - cy;
                return w;
            }
        } else if (point_in(x, y, w->x, w->y, w->w, w->h)) {
            if (lx) *lx = x - w->x;
            if (ly) *ly = y - w->y;
            return w;
        }
    }
    return NULL;
}

static void clamp_window(window_t *w)
{
    int wx, wy, ww, wh;
    desktop_workspace(&wx, &wy, &ww, &wh);

    if (w->w < w->min_w) w->w = w->min_w;
    if (w->h < w->min_h) w->h = w->min_h;
    if (w->w > ww) w->w = ww;
    if (w->h > wh) w->h = wh;

    if (w->x + w->w > wx + ww) w->x = wx + ww - w->w;
    if (w->y + w->h > wy + wh) w->y = wy + wh - w->h;
    if (w->x < wx) w->x = wx;
    if (w->y < wy) w->y = wy;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void wm_init(void)
{
    memset(windows, 0, sizeof(windows));
    memset(order, 0, sizeof(order));
    order_count = 0;
    drag_win = NULL;
    drag_mode = 0;
    last_buttons = 0;
    last_mx = last_my = -1;
}

window_t *wm_create(int app, const char *title, int x, int y, int w, int h,
                    int min_w, int min_h, win_draw_fn draw, win_key_fn key,
                    win_mouse_fn mouse, win_wheel_fn wheel, win_close_fn close,
                    void *state)
{
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i].used) continue;

        window_t *win = &windows[i];
        memset(win, 0, sizeof(window_t));
        win->used = 1;
        win->app = app;
        kstrncpy(win->title, title ? title : "Window", sizeof(win->title));
        win->x = x;
        win->y = y;
        win->w = max(w, 240);
        win->h = max(h, 160);
        win->min_w = max(min_w, 220);
        win->min_h = max(min_h, 140);
        win->visible = 1;
        win->minimized = 0;
        win->maximized = 0;
        win->focused = 0;
        win->state = state;
        win->draw = draw;
        win->key = key;
        win->mouse = mouse;
        win->wheel = wheel;
        win->close = close;

        clamp_window(win);

        if (order_count < WM_MAX_WINDOWS)
            order[order_count++] = i;

        wm_focus(win);
        return win;
    }

    klog(KLOG_LEVEL_WARN, "window table full");
    return NULL;
}

window_t *wm_find(int app)
{
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        if (windows[i].used && windows[i].app == app) return &windows[i];
    return NULL;
}

void wm_focus(window_t *w)
{
    if (!w || !w->used) return;

    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        windows[i].focused = 0;

    w->focused = 1;
    w->minimized = 0;
    w->visible = 1;
    order_raise(w);
    desktop_invalidate();
}

void wm_close(window_t *w)
{
    if (!w || !w->used) return;

    int was_focused = w->focused;
    if (w->close) w->close(w);

    int idx = order_index(w);
    order_remove_at(idx);

    memset(w, 0, sizeof(window_t));
    w->focused = 0;

    if (was_focused) {
        for (int i = order_count - 1; i >= 0; i--) {
            window_t *top = &windows[order[i]];
            if (top->used && top->visible && !top->minimized) {
                wm_focus(top);
                break;
            }
        }
    }
    desktop_invalidate();
}

void wm_minimize(window_t *w)
{
    if (!w || !w->used) return;
    int was_focused = w->focused;
    w->minimized = 1;
    w->focused = 0;
    drag_win = NULL;
    drag_mode = 0;

    if (was_focused) {
        for (int i = order_count - 1; i >= 0; i--) {
            window_t *top = &windows[order[i]];
            if (top->used && top->visible && !top->minimized) {
                wm_focus(top);
                break;
            }
        }
    }
    desktop_invalidate();
}

void wm_toggle_maximize(window_t *w)
{
    if (!w || !w->used) return;

    if (w->maximized) {
        w->x = w->restore_x;
        w->y = w->restore_y;
        w->w = w->restore_w;
        w->h = w->restore_h;
        w->maximized = 0;
    } else {
        w->restore_x = w->x;
        w->restore_y = w->y;
        w->restore_w = w->w;
        w->restore_h = w->h;

        int wx, wy, ww, wh;
        desktop_workspace(&wx, &wy, &ww, &wh);
        w->x = wx;
        w->y = wy;
        w->w = ww;
        w->h = wh;
        w->maximized = 1;
    }

    clamp_window(w);
    desktop_invalidate();
}

void wm_set_title(window_t *w, const char *title)
{
    if (!w || !w->used || !title) return;
    kstrncpy(w->title, title, sizeof(w->title));
    desktop_invalidate();
}

void wm_invalidate(void)
{
    desktop_invalidate();
}

/* ── Drawing ──────────────────────────────────────────────────────────────── */

static void draw_traffic_button(int x, int y, int kind, int hot, int focused)
{
    if (hot) {
        u32 bg = (kind == 0) ? theme.danger : theme.panel2;
        gfx_round_rect(x, y, WM_TITLE_BTN, WM_TITLE_BTN, 7, bg);
    }

    u32 fg = hot ? 0xFFFFFF : (focused ? theme.text_dim : theme.text_faint);

    if (kind == 0) {
        gfx_line(x + 7, y + 7, x + WM_TITLE_BTN - 7, y + WM_TITLE_BTN - 7, fg);
        gfx_line(x + WM_TITLE_BTN - 7, y + 7, x + 7, y + WM_TITLE_BTN - 7, fg);
    } else if (kind == 1) {
        gfx_round_outline(x + 7, y + 7, 9, 9, 2, fg);
        if (hot) gfx_fill(x + 9, y + 9, 5, 5, fg);
    } else {
        gfx_fill(x + 7, y + 14, 9, 2, fg);
    }
}

static void draw_window(window_t *w)
{
    if (!w->used || !w->visible || w->minimized) return;

    int x = w->x, y = w->y, ww = w->w, hh = w->h;
    int focused = w->focused;

    gfx_reset_clip();
    gfx_shadow(x, y, ww, hh, 12, 14, 115);

    /* Base body and rounded top strip.  The title strip is drawn with a
     * rounded top edge, then the body rectangle covers its lower corners. */
    u32 title_col = focused ? theme.title : gfx_lerp(theme.title, theme.bg0, 55);
    gfx_round_rect(x, y, ww, hh, 12, theme.win);
    gfx_round_rect(x, y, ww, WM_TITLE_H + 12, 12, title_col);
    gfx_fill(x, y + WM_TITLE_H, ww, hh - WM_TITLE_H, theme.win);

    /* Accent seam under the title bar. */
    gfx_fill(x + 10, y + WM_TITLE_H - 1, ww - 20, 1,
             focused ? gfx_lerp(theme.border, theme.accent, 150) : theme.border);

    u32 border = focused ? gfx_lerp(theme.border, theme.accent, 130) : theme.border;
    gfx_round_outline(x, y, ww, hh, 12, border);

    /* App icon + title. */
    app_icon_draw(w->app, x + 10, y + 6, WM_TITLE_BTN, focused);

    gfx_set_clip(x + 38, y + 4, ww - 38 - 96, WM_TITLE_H - 8);
    gfx_text(x + 42, y + 9, w->title, focused ? theme.text : theme.text_dim);
    gfx_reset_clip();

    /* Traffic lights. */
    int bx, bmx, bclose, by;
    title_buttons(w, &bx, &bmx, &bclose, &by);

    mouse_state_t *m = mouse_get();
    int mx = m ? m->x : -1;
    int my = m ? m->y : -1;

    draw_traffic_button(bx, by, 2, point_in(mx, my, bx, by, WM_TITLE_BTN, WM_TITLE_BTN), focused);
    draw_traffic_button(bmx, by, 1, point_in(mx, my, bmx, by, WM_TITLE_BTN, WM_TITLE_BTN), focused);
    draw_traffic_button(bclose, by, 0, point_in(mx, my, bclose, by, WM_TITLE_BTN, WM_TITLE_BTN), focused);

    /* Content. */
    int cx, cy, cw, ch;
    wm_content_rect(w, &cx, &cy, &cw, &ch);
    gfx_set_clip(cx, cy, cw, ch);
    gfx_fill(cx, cy, cw, ch, theme.win);
    if (w->draw) w->draw(w);
    gfx_reset_clip();
}

void wm_draw_all(void)
{
    for (int i = 0; i < order_count; i++) {
        window_t *w = &windows[order[i]];
        draw_window(w);
    }
}

/* ── Input ────────────────────────────────────────────────────────────────── */

int wm_mouse_down(int x, int y, int button)
{
    last_buttons |= button;

    if (button != MOUSE_BTN_LEFT) return 0;

    for (int i = order_count - 1; i >= 0; i--) {
        window_t *w = &windows[order[i]];
        if (!w->used || !w->visible || w->minimized) continue;
        if (!point_in(x, y, w->x, w->y, w->w, w->h)) continue;

        wm_focus(w);

        /* Title-bar buttons. */
        if (y < w->y + WM_TITLE_H) {
            int bx, bmx, bclose, by;
            title_buttons(w, &bx, &bmx, &bclose, &by);
            if (point_in(x, y, bclose, by, WM_TITLE_BTN, WM_TITLE_BTN)) {
                wm_close(w);
                return 1;
            }
            if (point_in(x, y, bmx, by, WM_TITLE_BTN, WM_TITLE_BTN)) {
                wm_toggle_maximize(w);
                return 1;
            }
            if (point_in(x, y, bx, by, WM_TITLE_BTN, WM_TITLE_BTN)) {
                wm_minimize(w);
                return 1;
            }
        }

        /* Resize grips: right, bottom, bottom-right, left. */
        if (!w->maximized) {
            int right  = (x >= w->x + w->w - WM_RESIZE_GRIP);
            int left   = (x <= w->x + WM_RESIZE_GRIP) && (y >= w->y + WM_TITLE_H);
            int bottom = (y >= w->y + w->h - WM_RESIZE_GRIP);
            int mode = 0;
            if (right && bottom) mode = 4;
            else if (right) mode = 2;
            else if (bottom) mode = 3;
            else if (left) mode = 5;

            if (mode) {
                drag_win = w;
                drag_mode = mode;
                drag_mx = x;
                drag_my = y;
                drag_x = w->x;
                drag_y = w->y;
                drag_w = w->w;
                drag_h = w->h;
                return 1;
            }
        }

        /* Title bar: start a move. */
        if (y < w->y + WM_TITLE_H) {
            if (w->maximized) {
                /* Dragging a maximized window restores it first. */
                wm_toggle_maximize(w);
            }
            drag_win = w;
            drag_mode = 1;
            drag_off_x = x - w->x;
            drag_off_y = y - w->y;
            if (drag_off_x > w->w - 40) drag_off_x = w->w / 2;
            return 1;
        }

        /* Content. */
        int cx, cy, cw, ch;
        wm_content_rect(w, &cx, &cy, &cw, &ch);
        if (w->mouse && point_in(x, y, cx, cy, cw, ch))
            w->mouse(w, x - cx, y - cy, button, 1);

        return 1;
    }

    return 0;
}

void wm_mouse_move(int x, int y, int buttons)
{
    last_mx = x;
    last_my = y;
    last_buttons = buttons;

    if (drag_win && drag_win->used) {
        window_t *w = drag_win;

        if (drag_mode == 1) {
            int nx = x - drag_off_x;
            int ny = y - drag_off_y;
            int wx, wy, ww, wh;
            desktop_workspace(&wx, &wy, &ww, &wh);
            if (nx < wx - w->w + 90) nx = wx - w->w + 90;
            if (nx > wx + ww - 90) nx = wx + ww - 90;
            if (ny < wy) ny = wy;
            if (ny > wy + wh - 40) ny = wy + wh - 40;
            w->x = nx;
            w->y = ny;
        } else {
            int dx = x - drag_mx;
            int dy = y - drag_my;

            if (drag_mode == 2 || drag_mode == 4)
                w->w = max(w->min_w, drag_w + dx);
            if (drag_mode == 3 || drag_mode == 4)
                w->h = max(w->min_h, drag_h + dy);
            if (drag_mode == 5) {
                int nw = max(w->min_w, drag_w - dx);
                w->x = drag_x + (drag_w - nw);
                w->w = nw;
            }

            int wx, wy, ww, wh;
            desktop_workspace(&wx, &wy, &ww, &wh);
            if (w->w > ww - (w->x - wx)) w->w = ww - (w->x - wx);
            if (w->h > wh - (w->y - wy)) w->h = wh - (w->y - wy);
            if (w->w < w->min_w) w->w = w->min_w;
            if (w->h < w->min_h) w->h = w->min_h;
        }

        desktop_invalidate();
        return;
    }

    int lx, ly;
    window_t *w = window_at(x, y, 1, &lx, &ly);
    if (w && w->mouse) w->mouse(w, lx, ly, 0, 0);

    desktop_invalidate();
}

void wm_mouse_up(int x, int y, int button)
{
    last_buttons &= ~button;

    if (drag_win) {
        drag_win = NULL;
        drag_mode = 0;
        desktop_invalidate();
    }

    if (button == MOUSE_BTN_LEFT) {
        int lx, ly;
        window_t *w = window_at(x, y, 1, &lx, &ly);
        if (w && w->mouse) w->mouse(w, lx, ly, button, 0);
    }
}

int wm_key(const key_event_t *ev)
{
    window_t *w = wm_focused();
    if (!w || !w->used || !w->visible || w->minimized) return 0;
    if (!w->key) return 0;
    w->key(w, ev);
    return 1;
}

int wm_wheel(int delta, int x, int y)
{
    int lx, ly;
    window_t *w = window_at(x, y, 1, &lx, &ly);
    if (!w) return 0;
    if (w->wheel) {
        w->wheel(w, delta, lx, ly);
        return 1;
    }
    return 1;
}

int wm_count(void) { return order_count; }

window_t *wm_at(int index)
{
    if (index < 0 || index >= order_count) return NULL;
    return &windows[order[index]];
}

window_t *wm_focused(void)
{
    for (int i = order_count - 1; i >= 0; i--) {
        window_t *w = &windows[order[i]];
        if (w->used && w->focused) return w;
    }
    return NULL;
}

void wm_focus_next(void)
{
    for (int i = 0; i < order_count; i++) {
        window_t *w = &windows[order[i]];
        if (w->used && !w->minimized) {
            wm_focus(w);
            return;
        }
    }
}

void wm_focus_prev(void)
{
    for (int i = order_count - 1; i >= 0; i--) {
        window_t *w = &windows[order[i]];
        if (w->used && !w->minimized) {
            wm_focus(w);
            return;
        }
    }
}

int wm_is_dragging(void) { return drag_win != NULL; }

int wm_hit_window(int x, int y, int *local_x, int *local_y)
{
    return window_at(x, y, 1, local_x, local_y) != NULL;
}

void wm_content_rect(window_t *w, int *x, int *y, int *cw, int *ch)
{
    if (!w) {
        if (x) *x = 0;
        if (y) *y = 0;
        if (cw) *cw = 0;
        if (ch) *ch = 0;
        return;
    }
    if (x)  *x  = w->x + 1;
    if (y)  *y  = w->y + WM_TITLE_H;
    if (cw) *cw = max(1, w->w - 2);
    if (ch) *ch = max(1, w->h - WM_TITLE_H - 1);
}

void wm_snap_workspace(void)
{
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        window_t *w = &windows[i];
        if (!w->used) continue;
        if (w->maximized) {
            int wx, wy, ww, wh;
            desktop_workspace(&wx, &wy, &ww, &wh);
            w->x = wx;
            w->y = wy;
            w->w = ww;
            w->h = wh;
        } else {
            clamp_window(w);
        }
    }
    desktop_invalidate();
}
