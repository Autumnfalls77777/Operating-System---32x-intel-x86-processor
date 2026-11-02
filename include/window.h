/* ============================================================================
 * include/window.h  -  Window manager
 * ============================================================================ */

#ifndef NOVA_WINDOW_H
#define NOVA_WINDOW_H

#include "types.h"
#include "keys.h"
#include "gfx.h"

#define WM_MAX_WINDOWS 16
#define WM_TITLE_H     34

typedef struct window window_t;

typedef void (*win_draw_fn)(window_t *w);
typedef void (*win_key_fn)(window_t *w, const key_event_t *ev);
typedef void (*win_mouse_fn)(window_t *w, int x, int y, int button, int pressed);
typedef void (*win_wheel_fn)(window_t *w, int delta, int x, int y);
typedef void (*win_close_fn)(window_t *w);

struct window {
    int used;
    int app;
    char title[64];
    int x, y, w, h;
    int min_w, min_h;
    int visible;
    int minimized;
    int maximized;
    int restore_x, restore_y, restore_w, restore_h;
    int focused;
    void *state;

    win_draw_fn  draw;
    win_key_fn   key;
    win_mouse_fn mouse;
    win_wheel_fn wheel;
    win_close_fn close;
};

void      wm_init(void);
window_t *wm_create(int app, const char *title, int x, int y, int w, int h,
                    int min_w, int min_h, win_draw_fn draw, win_key_fn key,
                    win_mouse_fn mouse, win_wheel_fn wheel, win_close_fn close,
                    void *state);
window_t *wm_find(int app);
void      wm_focus(window_t *w);
void      wm_close(window_t *w);
void      wm_minimize(window_t *w);
void      wm_toggle_maximize(window_t *w);
void      wm_set_title(window_t *w, const char *title);
void      wm_invalidate(void);

void      wm_draw_all(void);
int       wm_mouse_down(int x, int y, int button);
void      wm_mouse_move(int x, int y, int buttons);
void      wm_mouse_up(int x, int y, int button);
int       wm_key(const key_event_t *ev);
int       wm_wheel(int delta, int x, int y);
int       wm_count(void);
window_t *wm_at(int index);
window_t *wm_focused(void);
void      wm_focus_next(void);
void      wm_focus_prev(void);
int       wm_is_dragging(void);
int       wm_hit_window(int x, int y, int *local_x, int *local_y);
void      wm_content_rect(window_t *w, int *x, int *y, int *cw, int *ch);
void      wm_snap_workspace(void);

#endif /* NOVA_WINDOW_H */
