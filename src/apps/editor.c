/* ============================================================================
 * src/apps/editor.c  -  Text editor with line numbers and syntax tinting
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

#define ED_LINE_H 18
#define ED_GUTTER 46

typedef struct {
    char path[VFS_PATH_MAX];
    char text[VFS_MAX_FILE_SIZE + 1];
    int  len;
    int  cursor;
    int  scroll;
    int  hscroll;
    int  dirty;
    int  readonly;
    u32  last_save;
} editor_state_t;

/* â”€â”€ Text helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static int ed_line_start(const char *s, int pos)
{
    while (pos > 0 && s[pos - 1] != '\n') pos--;
    return pos;
}

static int ed_line_end(const char *s, int len, int pos)
{
    while (pos < len && s[pos] != '\n') pos++;
    return pos;
}

static int ed_line_index(const char *s, int pos)
{
    int line = 0;
    for (int i = 0; i < pos; i++)
        if (s[i] == '\n') line++;
    return line;
}

static int ed_line_start_at(const char *s, int len, int index)
{
    int pos = 0;
    while (index-- > 0 && pos < len) {
        while (pos < len && s[pos] != '\n') pos++;
        if (pos < len) pos++;
    }
    return pos;
}

static int ed_total_lines(const char *s, int len)
{
    int lines = 1;
    for (int i = 0; i < len; i++)
        if (s[i] == '\n') lines++;
    return lines;
}

static void ed_sync_title(window_t *w, editor_state_t *e)
{
    char name[64];
    const char *base = kstrrchr(e->path, '/');
    base = base ? base + 1 : e->path;
    ksnprintf(name, sizeof(name), "Editor - %s%s", base[0] ? base : "untitled",
              e->dirty ? " *" : "");
    wm_set_title(w, name);
}

/* â”€â”€ State â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void ed_set_cursor(editor_state_t *e, int pos)
{
    e->cursor = clamp(pos, 0, e->len);
}

static void ed_insert(editor_state_t *e, const char *data, int count)
{
    if (count <= 0) return;
    if (e->readonly) return;
    if (e->len + count > VFS_MAX_FILE_SIZE) return;

    for (int i = e->len; i >= e->cursor; i--)
        e->text[i + count] = e->text[i];

    memcpy(e->text + e->cursor, data, (size_t)count);
    e->len += count;
    e->cursor += count;
    e->text[e->len] = '\0';
    e->dirty = 1;
}

static void ed_delete_range(editor_state_t *e, int from, int to)
{
    if (e->readonly) return;
    if (from < 0) from = 0;
    if (to > e->len) to = e->len;
    if (from >= to) return;

    int count = to - from;
    for (int i = from; i + count <= e->len; i++)
        e->text[i] = e->text[i + count];
    e->len -= count;
    e->cursor = from;
    e->text[e->len] = '\0';
    e->dirty = 1;
}

static void ed_move_vertical(editor_state_t *e, int delta)
{
    int start = ed_line_start(e->text, e->cursor);
    int col = e->cursor - start;
    int line = ed_line_index(e->text, e->cursor);
    int target = line + delta;
    int total = ed_total_lines(e->text, e->len);
    if (target < 0) target = 0;
    if (target >= total) target = total - 1;

    int tstart = ed_line_start_at(e->text, e->len, target);
    int tend = ed_line_end(e->text, e->len, tstart);
    e->cursor = tstart + min(col, tend - tstart);
}

static void ed_load(window_t *w, editor_state_t *e, const char *path)
{
    kstrncpy(e->path, path ? path : "", sizeof(e->path));
    e->readonly = 0;

    if (!e->path[0]) {
        const char *sample =
            "/* Altitude OS scratch buffer */\n"
            "\n"
            "Write code, notes or kernel experiments here.\n"
            "Ctrl+S saves to the RAM filesystem.\n"
            "\n"
            "Try opening /home/dev/hello.c or /etc/motd.\n";
        kstrcpy(e->text, sample);
        e->len = kstrlen(e->text);
        e->dirty = 1;
    } else {
        int n = vfs_read_file(e->path, "/", e->text, sizeof(e->text));
        if (n < 0) {
            e->text[0] = '\0';
            e->len = 0;
            toast_show("Editor", "Could not open the file", TOAST_ERROR);
        } else {
            e->len = n;
        }
        int node = vfs_lookup(e->path, "/");
        if (node >= 0 && vfs_type_name(node)[0] == 'p') {
            e->readonly = 1;
            toast_show("Editor", "This is a generated /proc file - read only", TOAST_WARN);
        }
    }

    e->cursor = 0;
    e->scroll = 0;
    e->hscroll = 0;
    e->dirty = 0;
    ed_sync_title(w, e);
}

static void ed_save(window_t *w, editor_state_t *e)
{
    if (e->readonly) {
        toast_show("Editor", "Read-only file", TOAST_WARN);
        return;
    }
    if (!e->path[0]) kstrcpy(e->path, "/home/dev/untitled.txt");

    int n = vfs_write_file(e->path, "/", e->text, (u32)e->len, 0);
    if (n < 0) {
        toast_show("Editor", "Save failed", TOAST_ERROR);
        return;
    }
    e->dirty = 0;
    e->last_save = timer_ticks();
    ed_sync_title(w, e);
    toast_show("Editor", "Saved to the RAM filesystem", TOAST_SUCCESS);
}

/* â”€â”€ Drawing â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static int ed_line_color(const char *line, int len)
{
    int i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= len) return 0;
    if (line[i] == '#') return 1;
    if (line[i] == '/' && i + 1 < len && line[i + 1] == '/') return 2;
    if (line[i] == '/' && i + 1 < len && line[i + 1] == '*') return 2;
    if (line[i] == '*' && i + 1 < len && line[i + 1] == '/') return 2;
    return 0;
}

static void ed_draw(window_t *w)
{
    editor_state_t *e = (editor_state_t *)w->state;
    if (!e) return;

    int x = CX(w), y = CY(w), cw = CWD(w), ch = CHT(w);
    gfx_fill(x, y, cw, ch, theme.win);

    /* Toolbar */
    int bar_h = 36;
    gfx_fill(x, y, cw, bar_h, theme.panel);
    gfx_fill(x, y + bar_h - 1, cw, 1, theme.border);
    gfx_fill(x + 10, y + 10, 4, 14, theme.accent);
    gfx_text(x + 22, y + 10, e->path[0] ? e->path : "untitled", theme.text);
    if (e->dirty) gfx_circle_fill(x + cw - 138, y + 18, 4, theme.warn);

    int save_x = x + cw - 124;
    int revert_x = x + cw - 64;
    mouse_state_t *m = mouse_get();
    int mx = m ? m->x : -1, my = m ? m->y : -1;
    int hot_save = app_hit(mx, my, save_x, y + 6, 54, 24);
    int hot_rev  = app_hit(mx, my, revert_x, y + 6, 54, 24);

    gfx_round_rect(save_x, y + 6, 54, 24, 6, hot_save ? gfx_lerp(theme.accent, 0xFFFFFF, 30) : theme.accent);
    gfx_text(save_x + (54 - gfx_text_width("Save")) / 2, y + 10, "Save", 0x06101F);
    gfx_round_rect(revert_x, y + 6, 54, 24, 6, hot_rev ? theme.panel2 : theme.bg1);
    gfx_round_outline(revert_x, y + 6, 54, 24, 6, theme.border);
    gfx_text(revert_x + (54 - gfx_text_width("Revert")) / 2, y + 10, "Revert", theme.text_dim);

    /* Code area */
    int area_y = y + bar_h;
    int area_h = ch - bar_h - APP_STATUS_H;
    int rows = max(1, area_h / ED_LINE_H);
    int cursor_line = ed_line_index(e->text, e->cursor);
    int total_lines = ed_total_lines(e->text, e->len);

    if (cursor_line < e->scroll) e->scroll = cursor_line;
    if (cursor_line >= e->scroll + rows) e->scroll = cursor_line - rows + 1;
    if (e->scroll > max(0, total_lines - rows)) e->scroll = max(0, total_lines - rows);
    if (e->scroll < 0) e->scroll = 0;

    int text_x = x + ED_GUTTER;
    int text_w = cw - ED_GUTTER - 10;
    int visible_cols = max(8, text_w / 8);

    int cursor_col = e->cursor - ed_line_start(e->text, e->cursor);
    if (cursor_col < e->hscroll) e->hscroll = max(0, cursor_col - 2);
    if (cursor_col >= e->hscroll + visible_cols)
        e->hscroll = cursor_col - visible_cols + 2;
    if (e->hscroll < 0) e->hscroll = 0;

    gfx_fill(x, area_y, cw, area_h, theme.term_bg);
    gfx_fill(x + ED_GUTTER - 1, area_y, 1, area_h, theme.border);

    int line_start = ed_line_start_at(e->text, e->len, e->scroll);
    int line_no = e->scroll;

    for (int row = 0; row < rows; row++) {
        if (line_no >= total_lines) break;
        int ry = area_y + row * ED_LINE_H;
        int line_end = ed_line_end(e->text, e->len, line_start);
        int is_current = (line_no == cursor_line);

        if (is_current)
            gfx_fill(x + ED_GUTTER, ry, cw - ED_GUTTER, ED_LINE_H, theme.panel);

        char num[16];
        ksnprintf(num, sizeof(num), "%d", line_no + 1);
        gfx_text_right(x + ED_GUTTER - 12, ry + 1, num,
                       is_current ? theme.accent : theme.text_faint);

        int llen = line_end - line_start;
        int lcolor = ed_line_color(e->text + line_start, llen);
        u32 color = theme.term_text;
        if (lcolor == 1) color = theme.accent;
        else if (lcolor == 2) color = theme.success;

        char buf[512];
        int n = 0;
        int col = 0;
        for (int i = line_start; i < line_end && n < (int)sizeof(buf) - 5; i++) {
            char c = e->text[i];
            if (col++ < e->hscroll) continue;
            if (c == '\t') {
                for (int s = 0; s < 4 && n < (int)sizeof(buf) - 5; s++) buf[n++] = ' ';
            } else {
                buf[n++] = c;
            }
            if (n >= visible_cols + 1) break;
        }
        buf[n] = '\0';

        gfx_set_clip(text_x, ry, text_w, ED_LINE_H);
        gfx_text(text_x, ry + 1, buf, color);
        gfx_reset_clip();

        line_start = (line_end < e->len) ? line_end + 1 : line_end;
        line_no++;
    }

    /* Caret */
    if ((timer_ticks() / 50) % 2 == 0 && cursor_line >= e->scroll &&
        cursor_line < e->scroll + rows) {
        int caret_x = text_x + (cursor_col - e->hscroll) * 8;
        int caret_y = area_y + (cursor_line - e->scroll) * ED_LINE_H + 1;
        gfx_fill(caret_x, caret_y, 2, 16, theme.accent);
    }

    /* Status bar */
    char left[128];
    ksnprintf(left, sizeof(left), "Ln %d, Col %d  |  %d bytes%s",
              cursor_line + 1, cursor_col + 1, e->len,
              e->readonly ? "  |  read-only" : "");
    app_status(w, left, "Ctrl+S save   Ctrl+R revert", theme.text_faint);
}

/* â”€â”€ Input â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void ed_mouse(window_t *w, int mx, int my, int button, int pressed)
{
    editor_state_t *e = (editor_state_t *)w->state;
    if (!e || button != MOUSE_BTN_LEFT || !pressed) return;

    int x = CX(w), y = CY(w), cw = CWD(w);
    int save_x = x + cw - 124;
    int revert_x = x + cw - 64;
    int ax = mx + x, ay = my + y;

    if (app_hit(ax, ay, save_x, y + 6, 54, 24)) { ed_save(w, e); return; }
    if (app_hit(ax, ay, revert_x, y + 6, 54, 24)) {
        if (e->dirty) toast_show("Editor", "Reverted to the saved version", TOAST_WARN);
        char path[VFS_PATH_MAX];
        kstrcpy(path, e->path);
        ed_load(w, e, path);
        return;
    }

    int area_y = y + 36;
    if (ay < area_y) return;

    int row = (ay - area_y) / ED_LINE_H;
    int line = e->scroll + row;
    if (line < 0 || line >= ed_total_lines(e->text, e->len)) return;

    int start = ed_line_start_at(e->text, e->len, line);
    int end = ed_line_end(e->text, e->len, start);
    int col = (ax - (x + ED_GUTTER)) / 8 + e->hscroll;
    if (col < 0) col = 0;
    if (col > end - start) col = end - start;
    ed_set_cursor(e, start + col);
}

static void ed_key(window_t *w, const key_event_t *ev)
{
    editor_state_t *e = (editor_state_t *)w->state;
    if (!e) return;

    if (ev->mods & MOD_CTRL) {
        switch (ev->ch) {
        case 's': case 'S': ed_save(w, e); return;
        case 'r': case 'R': {
            char path[VFS_PATH_MAX];
            kstrcpy(path, e->path);
            ed_load(w, e, path);
            return;
        }
        default: break;
        }
    }

    switch (ev->key) {
    case KEY_LEFT:
        if (e->cursor > 0) e->cursor--;
        return;
    case KEY_RIGHT:
        if (e->cursor < e->len) e->cursor++;
        return;
    case KEY_UP:
        ed_move_vertical(e, -1);
        return;
    case KEY_DOWN:
        ed_move_vertical(e, 1);
        return;
    case KEY_HOME:
        e->cursor = ed_line_start(e->text, e->cursor);
        return;
    case KEY_END:
        e->cursor = ed_line_end(e->text, e->len, e->cursor);
        return;
    case KEY_PAGEUP:
        for (int i = 0; i < 16; i++) ed_move_vertical(e, -1);
        return;
    case KEY_PAGEDOWN:
        for (int i = 0; i < 16; i++) ed_move_vertical(e, 1);
        return;
    case KEY_BACKSPACE:
        if (e->cursor > 0) ed_delete_range(e, e->cursor - 1, e->cursor);
        return;
    case KEY_DELETE:
        if (e->cursor < e->len) ed_delete_range(e, e->cursor, e->cursor + 1);
        return;
    case KEY_ENTER: {
        char nl = '\n';
        ed_insert(e, &nl, 1);
        return;
    }
    case KEY_TAB: {
        const char *spaces = "    ";
        ed_insert(e, spaces, 4);
        return;
    }
    default:
        break;
    }

    if (ev->ch >= 32 && ev->ch <= 126) {
        char c = (char)ev->ch;
        ed_insert(e, &c, 1);
    }
}

static void ed_wheel(window_t *w, int delta, int x, int y)
{
    editor_state_t *e = (editor_state_t *)w->state;
    UNUSED(x); UNUSED(y);
    if (!e) return;
    e->scroll -= delta > 0 ? 3 : -3;
    if (e->scroll < 0) e->scroll = 0;
    int total = ed_total_lines(e->text, e->len);
    int max_scroll = max(0, total - 1);
    if (e->scroll > max_scroll) e->scroll = max_scroll;
}

static void ed_close(window_t *w)
{
    if (w->state) {
        kfree(w->state);
        w->state = NULL;
    }
}

/* â”€â”€ Entry points â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

void app_editor_load(const char *path)
{
    window_t *w = wm_find(APP_EDITOR);
    if (w) {
        editor_state_t *e = (editor_state_t *)w->state;
        if (e) {
            if (e->dirty) toast_show("Editor", "Unsaved changes were replaced", TOAST_WARN);
            ed_load(w, e, path);
        }
        wm_focus(w);
        return;
    }

    editor_state_t *e = (editor_state_t *)kzalloc(sizeof(editor_state_t));
    if (!e) {
        toast_show("Editor", "Not enough memory", TOAST_ERROR);
        return;
    }

    int ww = min(940, max(560, gfx.width - 180));
    int wh = min(620, max(380, gfx.height - 180));
    int wx = max(45, (gfx.width - ww) / 2 - 10);
    int wy = max(58, (gfx.height - wh) / 2 - 8);

    window_t *win = wm_create(APP_EDITOR, "Editor", wx, wy, ww, wh,
                              500, 300, ed_draw, ed_key, ed_mouse, ed_wheel,
                              ed_close, e);
    if (!win) {
        kfree(e);
        return;
    }

    const char *target = (path && *path) ? path : "/home/dev/notes.md";
    int node = vfs_lookup(target, "/");
    if (node < 0) target = "";
    ed_load(win, e, target);
}

void app_editor_open(void)
{
    app_editor_load(NULL);
}
