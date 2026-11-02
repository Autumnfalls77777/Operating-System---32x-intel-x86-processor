/* ============================================================================
 * src/apps/files.c  -  Files: browse and manage the NOVA virtual filesystem
 * ============================================================================ */

#include "kernel.h"
#include "vfs.h"
#include "theme.h"
#include "gfx.h"
#include "widgets.h"
#include "mouse.h"
#include "timer.h"
#include "memory.h"
#include "apps.h"
#include "desktop.h"
#include "apps_internal.h"

#define FILES_SIDEBAR_W 154
#define FILES_ROW_H     26

typedef struct {
    char cwd[VFS_PATH_MAX];
    int  entries[VFS_MAX_NODES];
    int  count;
    int  selected;
    int  scroll;
    int  visible_rows;
    int  last_clicked;
    u32  last_click_tick;
} files_state_t;

static const char *places[] = {
    "/", "/home/dev", "/etc", "/proc", "/usr", "/tmp"
};
static const char *place_names[] = {
    "Filesystem", "Home", "Config", "Processes", "System", "Temporary"
};
#define PLACE_COUNT ((int)(sizeof(places) / sizeof(places[0])))

/* ── State helpers ────────────────────────────────────────────────────────── */

static void files_refresh(files_state_t *f)
{
    int node = vfs_lookup(f->cwd, "/");
    if (node < 0 || !vfs_is_dir(node)) {
        kstrcpy(f->cwd, "/home/dev");
        node = vfs_lookup(f->cwd, "/");
    }
    if (node < 0) node = vfs_root();

    vfs_path(node, f->cwd, sizeof(f->cwd));
    f->count = vfs_list(node, f->entries, VFS_MAX_NODES);
    if (f->selected >= f->count) f->selected = f->count - 1;
    if (f->selected < 0) f->selected = 0;

    int max_scroll = max(0, f->count - f->visible_rows);
    if (f->scroll > max_scroll) f->scroll = max_scroll;
    if (f->scroll < 0) f->scroll = 0;
}

static void files_goto(files_state_t *f, const char *path)
{
    int node = vfs_lookup(path, f->cwd);
    if (node < 0 || !vfs_is_dir(node)) {
        toast_show("Files", "That directory is not available", TOAST_WARN);
        return;
    }
    vfs_path(node, f->cwd, sizeof(f->cwd));
    f->selected = 0;
    f->scroll = 0;
    f->last_clicked = -1;
    files_refresh(f);

    window_t *w = wm_find(APP_FILES);
    if (w) {
        char title[96];
        ksnprintf(title, sizeof(title), "Files - %s", f->cwd);
        wm_set_title(w, title);
    }
}

static void files_up(files_state_t *f)
{
    int node = vfs_lookup(f->cwd, "/");
    if (node < 0) return;
    int parent = vfs_parent(node);
    if (parent < 0) return;
    char path[VFS_PATH_MAX];
    vfs_path(parent, path, sizeof(path));
    files_goto(f, path);
}

static void files_activate(files_state_t *f, int index)
{
    if (index < 0 || index >= f->count) return;
    int node = f->entries[index];

    if (vfs_is_dir(node)) {
        char path[VFS_PATH_MAX];
        vfs_path(node, path, sizeof(path));
        files_goto(f, path);
        return;
    }

    char path[VFS_PATH_MAX];
    vfs_path(node, path, sizeof(path));
    app_editor_open_file(path);
}

static const char *files_selected_path(files_state_t *f, char *buf, int cap)
{
    if (f->selected < 0 || f->selected >= f->count) return NULL;
    vfs_path(f->entries[f->selected], buf, cap);
    return buf;
}

static int unique_name(files_state_t *f, const char *base, const char *ext,
                       char *out, int cap)
{
    for (int i = 0; i < 100; i++) {
        if (i == 0) ksnprintf(out, cap, "%s%s", base, ext);
        else       ksnprintf(out, cap, "%s%d%s", base, i + 1, ext);

        char full[VFS_PATH_MAX];
        ksnprintf(full, sizeof(full), "%s/%s", kstrcmp(f->cwd, "/") == 0 ? "" : f->cwd, out);
        if (vfs_lookup(full, "/") < 0) return 0;
    }
    return -1;
}

/* ── Layout helpers ───────────────────────────────────────────────────────── */

typedef struct {
    int up_x, home_x;
    int newdir_x, newfile_x, del_x, edit_x, term_x;
    int btn_y;
} files_buttons_t;

static void files_buttons(window_t *w, files_buttons_t *b)
{
    int x = CX(w);
    int y = CY(w) + 7;
    b->btn_y = y;
    b->up_x = x + 10;
    b->home_x = b->up_x + 44;

    int cw = CWD(w);
    int rx = x + cw - 10;
    b->term_x = rx - 44;
    b->edit_x = b->term_x - 48;
    b->del_x = b->edit_x - 44;
    b->newfile_x = b->del_x - 52;
    b->newdir_x = b->newfile_x - 50;
}

static void small_btn(int x, int y, int w, const char *label, int hot, int primary)
{
    u32 base = primary ? theme.accent : theme.panel2;
    if (hot) base = gfx_lerp(base, 0xFFFFFF, primary ? 30 : 24);
    gfx_round_rect(x, y, w, 24, 6, base);
    gfx_round_outline(x, y, w, 24, 6, primary ? theme.accent : theme.border);
    gfx_text(x + (w - gfx_text_width(label)) / 2, y + 4, label,
             primary ? 0x06101F : theme.text);
}

/* ── Drawing ──────────────────────────────────────────────────────────────── */

static void draw_folder_icon(int x, int y, int s, u32 color)
{
    gfx_round_rect(x, y + s / 4, s * 2 / 3, s / 3, 2, color);
    gfx_round_rect(x, y + s / 3, s, s * 2 / 3, 3, color);
    gfx_fill(x, y + s / 3, s, 1, gfx_lerp(color, 0xFFFFFF, 80));
}

static void draw_file_icon(int x, int y, int s, u32 color)
{
    gfx_round_rect(x + s / 6, y, s * 2 / 3, s, 2, color);
    for (int i = 0; i < 3; i++)
        gfx_fill(x + s / 6 + 3, y + 4 + i * 4, s * 2 / 3 - 6, 1,
                 gfx_lerp(color, 0x000000, 110));
}

static void files_draw(window_t *w)
{
    files_state_t *f = (files_state_t *)w->state;
    if (!f) return;

    int x = CX(w), y = CY(w), cw = CWD(w), ch = CHT(w);
    gfx_fill(x, y, cw, ch, theme.win);

    /* Toolbar */
    int bar_h = 38;
    gfx_fill(x, y, cw, bar_h, theme.panel);
    gfx_fill(x, y + bar_h - 1, cw, 1, theme.border);

    files_buttons_t b;
    files_buttons(w, &b);
    mouse_state_t *m = mouse_get();
    int mx = m ? m->x : -1, my = m ? m->y : -1;

    small_btn(b.up_x, b.btn_y, 40, "Up", app_hit(mx, my, b.up_x, b.btn_y, 40, 24), 0);
    small_btn(b.home_x, b.btn_y, 46, "Home", app_hit(mx, my, b.home_x, b.btn_y, 46, 24), 0);
    small_btn(b.newdir_x, b.btn_y, 46, "+Dir", app_hit(mx, my, b.newdir_x, b.btn_y, 46, 24), 0);
    small_btn(b.newfile_x, b.btn_y, 48, "+File", app_hit(mx, my, b.newfile_x, b.btn_y, 48, 24), 0);
    small_btn(b.del_x, b.btn_y, 40, "Del", app_hit(mx, my, b.del_x, b.btn_y, 40, 24), 0);
    small_btn(b.edit_x, b.btn_y, 44, "Edit", app_hit(mx, my, b.edit_x, b.btn_y, 44, 24), 0);
    small_btn(b.term_x, b.btn_y, 44, "Term", app_hit(mx, my, b.term_x, b.btn_y, 44, 24), 1);

    int path_x = b.home_x + 54;
    int path_w = b.newdir_x - path_x - 8;
    if (path_w < 40) path_w = 40;
    gfx_set_clip(path_x, y, path_w, bar_h);
    gfx_fill(path_x, y + 8, path_w, 22, theme.bg0);
    gfx_round_rect(path_x, y + 8, path_w, 22, 5, theme.bg0);
    gfx_text(path_x + 8, y + 11, f->cwd, theme.text_dim);
    gfx_reset_clip();

    /* Sidebar */
    int side_y = y + bar_h;
    int side_h = ch - bar_h - APP_STATUS_H;
    int side_w = FILES_SIDEBAR_W;
    gfx_fill(x, side_y, side_w, side_h, theme.panel);
    gfx_fill(x + side_w, side_y, 1, side_h, theme.border);

    gfx_text(x + 14, side_y + 10, "PLACES", theme.text_faint);

    int place_y = side_y + 32;
    for (int i = 0; i < PLACE_COUNT; i++) {
        int row_y = place_y + i * 30;
        int node = vfs_lookup(places[i], "/");
        int selected = (node >= 0 && vfs_lookup(f->cwd, "/") == node);
        int hot = app_hit(mx, my, x + 6, row_y, side_w - 12, 26);

        if (selected) gfx_round_rect(x + 6, row_y, side_w - 12, 26, 6, theme.accent_soft);
        else if (hot) gfx_round_rect(x + 6, row_y, side_w - 12, 26, 6, theme.panel2);

        draw_folder_icon(x + 14, row_y + 6, 14, theme.warn);
        gfx_text(x + 36, row_y + 5, place_names[i],
                 selected ? theme.text : theme.text_dim);
    }

    /* Storage summary at the bottom of the sidebar. */
    int sy = side_y + side_h - 70;
    gfx_fill(x + 12, sy - 8, side_w - 24, 1, theme.border);
    gfx_text(x + 14, sy, "RAM FILESYSTEM", theme.text_faint);
    gfx_textf(x + 14, sy + 18, theme.text_dim, "%d nodes", vfs_used_nodes());
    gfx_textf(x + 14, sy + 36, theme.text_faint, "%u bytes", vfs_used_bytes());

    /* File list */
    int lx = x + side_w + 1;
    int ly = side_y;
    int lw = cw - side_w - 1;
    int lh = side_h;

    gfx_fill(lx, ly, lw, 26, theme.bg0);
    gfx_text(lx + 12, ly + 5, "NAME", theme.text_faint);
    if (lw > 420) {
        gfx_text_right(lx + lw - 170, ly + 5, "SIZE", theme.text_faint);
        gfx_text_right(lx + lw - 96, ly + 5, "MODE", theme.text_faint);
    }
    gfx_fill(lx, ly + 25, lw, 1, theme.border);

    int list_y = ly + 26;
    int list_h = lh - 26;
    f->visible_rows = max(0, list_h / FILES_ROW_H);
    int max_scroll = max(0, f->count - f->visible_rows);
    if (f->scroll > max_scroll) f->scroll = max_scroll;
    if (f->selected < f->scroll) f->scroll = f->selected;
    if (f->selected >= f->scroll + f->visible_rows)
        f->scroll = f->selected - f->visible_rows + 1;
    if (f->scroll < 0) f->scroll = 0;

    if (f->count == 0) {
        gfx_text_center(lx + lw / 2, list_y + 40, "Empty directory", theme.text_faint);
    }

    for (int row = 0; row < f->visible_rows; row++) {
        int index = f->scroll + row;
        if (index >= f->count) break;

        int node = f->entries[index];
        int ry = list_y + row * FILES_ROW_H;
        int selected = (index == f->selected);
        int hot = app_hit(mx, my, lx, ry, lw, FILES_ROW_H);

        if (selected) gfx_fill(lx, ry, lw, FILES_ROW_H, theme.accent_soft);
        else if (hot) gfx_fill(lx, ry, lw, FILES_ROW_H, theme.panel2);

        if (vfs_is_dir(node)) draw_folder_icon(lx + 12, ry + 6, 15, theme.warn);
        else if (vfs_type_name(node)[0] == 'p') draw_file_icon(lx + 12, ry + 5, 15, theme.success);
        else draw_file_icon(lx + 12, ry + 5, 15, theme.text_dim);

        u32 name_color = vfs_is_dir(node) ? theme.text : theme.text_dim;
        if (vfs_type_name(node)[0] == 'p') name_color = theme.success;

        gfx_set_clip(lx + 32, ry, lw - (lw > 420 ? 200 : 40), FILES_ROW_H);
        gfx_text(lx + 32, ry + 5, vfs_name(node), name_color);
        gfx_reset_clip();

        if (lw > 420) {
            char size[24];
            vfs_format_size(vfs_size(node), size, sizeof(size));
            gfx_text_right(lx + lw - 170, ry + 5, size, theme.text_faint);

            char mode[16];
            vfs_format_mode(vfs_mode(node), mode);
            gfx_text_right(lx + lw - 96, ry + 5, mode,
                           vfs_is_dir(node) ? theme.warn : theme.text_faint);
        }
    }

    if (f->count > f->visible_rows)
        ui_scrollbar(lx + lw - 8, list_y + 4, 6, list_h - 8,
                     f->scroll, f->count, f->visible_rows);

    /* Status bar */
    char left[160];
    char right[64];
    if (f->selected >= 0 && f->selected < f->count) {
        int node = f->entries[f->selected];
        char size[24];
        vfs_format_size(vfs_size(node), size, sizeof(size));
        ksnprintf(left, sizeof(left), "%s  |  %s  |  %s",
                  vfs_name(node), size, vfs_type_name(node));
        ksnprintf(right, sizeof(right), "%d items", f->count);
    } else {
        ksnprintf(left, sizeof(left), "%d items", f->count);
        kstrcpy(right, f->cwd);
    }
    app_status(w, left, right, theme.text_faint);
}

/* ── Actions ──────────────────────────────────────────────────────────────── */

static void files_new_dir(files_state_t *f)
{
    char name[64], full[VFS_PATH_MAX];
    if (unique_name(f, "newdir", "", name, sizeof(name)) < 0) return;
    ksnprintf(full, sizeof(full), "%s/%s",
              kstrcmp(f->cwd, "/") == 0 ? "" : f->cwd, name);
    if (vfs_mkdir(full, "/") < 0) {
        toast_show("Files", "Could not create the directory", TOAST_ERROR);
        return;
    }
    files_refresh(f);
    toast_show("Files", "Directory created", TOAST_SUCCESS);
}

static void files_new_file(files_state_t *f)
{
    char name[64], full[VFS_PATH_MAX];
    if (unique_name(f, "untitled", ".txt", name, sizeof(name)) < 0) return;
    ksnprintf(full, sizeof(full), "%s/%s",
              kstrcmp(f->cwd, "/") == 0 ? "" : f->cwd, name);
    if (vfs_create(full, "/") < 0) {
        toast_show("Files", "Could not create the file", TOAST_ERROR);
        return;
    }
    files_refresh(f);
    toast_show("Files", "File created", TOAST_SUCCESS);
}

static void files_delete(files_state_t *f)
{
    char path[VFS_PATH_MAX];
    if (!files_selected_path(f, path, sizeof(path))) return;
    if (vfs_unlink(path, "/") < 0) {
        toast_show("Files", "Directory is not empty or cannot be removed", TOAST_ERROR);
        return;
    }
    toast_show("Files", "Deleted", TOAST_WARN);
    files_refresh(f);
}

static void files_edit(files_state_t *f)
{
    if (f->selected < 0 || f->selected >= f->count) return;
    int node = f->entries[f->selected];
    if (vfs_is_dir(node)) {
        files_activate(f, f->selected);
        return;
    }
    char path[VFS_PATH_MAX];
    vfs_path(node, path, sizeof(path));
    app_editor_open_file(path);
}

static void files_mouse(window_t *w, int mx, int my, int button, int pressed)
{
    files_state_t *f = (files_state_t *)w->state;
    if (!f) return;
    if (button != MOUSE_BTN_LEFT || !pressed) return;

    /* Window callbacks receive content-local coordinates. */
    int ax = mx + CX(w);
    int ay = my + CY(w);

    files_buttons_t b;
    files_buttons(w, &b);

    if (app_hit(ax, ay, b.up_x, b.btn_y, 40, 24)) { files_up(f); return; }
    if (app_hit(ax, ay, b.home_x, b.btn_y, 46, 24)) { files_goto(f, "/home/dev"); return; }
    if (app_hit(ax, ay, b.newdir_x, b.btn_y, 46, 24)) { files_new_dir(f); return; }
    if (app_hit(ax, ay, b.newfile_x, b.btn_y, 48, 24)) { files_new_file(f); return; }
    if (app_hit(ax, ay, b.del_x, b.btn_y, 40, 24)) { files_delete(f); return; }
    if (app_hit(ax, ay, b.edit_x, b.btn_y, 44, 24)) { files_edit(f); return; }
    if (app_hit(ax, ay, b.term_x, b.btn_y, 44, 24)) {
        app_terminal_open_at(f->cwd);
        return;
    }

    /* Sidebar */
    int place_y = APP_BAR_H + 32;
    for (int i = 0; i < PLACE_COUNT; i++) {
        int row_y = place_y + i * 30;
        if (app_hit(mx, my, 6, row_y, FILES_SIDEBAR_W - 12, 26)) {
            files_goto(f, places[i]);
            return;
        }
    }

    /* File rows */
    if (mx > FILES_SIDEBAR_W) {
        int list_y = APP_BAR_H + 26;
        if (my >= list_y) {
            int row = (my - list_y) / FILES_ROW_H;
            int index = f->scroll + row;
            if (index >= 0 && index < f->count && row < f->visible_rows) {
                u32 now = timer_ticks();
                if (f->last_clicked == index && now - f->last_click_tick < 45) {
                    f->selected = index;
                    files_activate(f, index);
                    f->last_clicked = -1;
                } else {
                    f->selected = index;
                    f->last_clicked = index;
                    f->last_click_tick = now;
                }
            }
        }
    }
}

static void files_key(window_t *w, const key_event_t *ev)
{
    files_state_t *f = (files_state_t *)w->state;
    if (!f) return;

    if (ev->mods & MOD_CTRL) {
        if (ev->ch == 'n' || ev->ch == 'N') files_new_dir(f);
        return;
    }

    switch (ev->key) {
    case KEY_UP:
        if (f->selected > 0) f->selected--;
        break;
    case KEY_DOWN:
        if (f->selected + 1 < f->count) f->selected++;
        break;
    case KEY_HOME:
        f->selected = 0;
        f->scroll = 0;
        break;
    case KEY_END:
        f->selected = max(0, f->count - 1);
        break;
    case KEY_PAGEUP:
        f->selected = max(0, f->selected - f->visible_rows);
        break;
    case KEY_PAGEDOWN:
        f->selected = min(f->count - 1, f->selected + f->visible_rows);
        break;
    case KEY_ENTER:
        files_activate(f, f->selected);
        break;
    case KEY_BACKSPACE:
        files_up(f);
        break;
    case KEY_DELETE:
        files_delete(f);
        break;
    case KEY_F5:
        files_refresh(f);
        break;
    case KEY_F2:
        files_edit(f);
        break;
    default:
        break;
    }
}

static void files_wheel(window_t *w, int delta, int x, int y)
{
    files_state_t *f = (files_state_t *)w->state;
    UNUSED(x); UNUSED(y);
    if (!f) return;
    f->scroll -= delta > 0 ? 2 : -2;
    int max_scroll = max(0, f->count - f->visible_rows);
    if (f->scroll > max_scroll) f->scroll = max_scroll;
    if (f->scroll < 0) f->scroll = 0;
}

static void files_close(window_t *w)
{
    if (w->state) {
        kfree(w->state);
        w->state = NULL;
    }
}

/* ── Entry points ─────────────────────────────────────────────────────────── */

void app_files_open_at(const char *path)
{
    window_t *w = wm_find(APP_FILES);
    if (w) {
        files_state_t *f = (files_state_t *)w->state;
        if (f && path && *path) files_goto(f, path);
        wm_focus(w);
        return;
    }

    files_state_t *f = (files_state_t *)kzalloc(sizeof(files_state_t));
    if (!f) {
        toast_show("Files", "Not enough memory", TOAST_ERROR);
        return;
    }
    kstrcpy(f->cwd, (path && *path) ? path : "/home/dev");
    f->selected = 0;
    f->scroll = 0;
    f->last_clicked = -1;
    f->visible_rows = 10;
    files_refresh(f);

    int ww = min(900, max(560, gfx.width - 160));
    int wh = min(600, max(360, gfx.height - 180));
    int wx = max(40, (gfx.width - ww) / 2 - 20);
    int wy = max(55, (gfx.height - wh) / 2 - 10);

    window_t *win = wm_create(APP_FILES, "Files", wx, wy, ww, wh,
                              520, 320, files_draw, files_key, files_mouse,
                              files_wheel, files_close, f);
    if (!win) {
        kfree(f);
        return;
    }

    char title[96];
    ksnprintf(title, sizeof(title), "Files - %s", f->cwd);
    wm_set_title(win, title);
}

void app_files_open(void)
{
    app_files_open_at("/home/dev");
}
