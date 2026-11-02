/* ============================================================================
 * src/apps/terminal.c  -  Terminal emulator and the interactive shell window
 * ============================================================================ */

#include "kernel.h"
#include "terminal.h"
#include "shell.h"
#include "theme.h"
#include "gfx.h"
#include "widgets.h"
#include "memory.h"
#include "timer.h"
#include "vfs.h"
#include "apps.h"
#include "desktop.h"
#include "apps_internal.h"

/* ── Line buffer management ───────────────────────────────────────────────── */

static void line_reset(terminal_t *t, int index)
{
    t->line_len[index] = 0;
    t->line_color[index] = TERM_C_DEFAULT;
    t->lines[index][0] = '\0';
}

void term_init(terminal_t *t)
{
    if (!t) return;
    memset(t, 0, sizeof(terminal_t));
    t->line_count = 1;
    line_reset(t, 0);
    kstrcpy(t->cwd, "/home/dev");
    t->history_pos = 0;
}

void term_clear(terminal_t *t)
{
    if (!t) return;
    for (int i = 0; i < TERM_MAX_LINES; i++)
        line_reset(t, i);
    t->line_count = 1;
    t->scroll = 0;
}

void term_newline(terminal_t *t)
{
    if (!t) return;

    if (t->line_count < TERM_MAX_LINES) {
        t->line_count++;
    } else {
        /* Scroll the buffer by one line. */
        memmove(t->lines[0], t->lines[1],
                (size_t)(TERM_MAX_LINES - 1) * TERM_MAX_COLS);
        memmove(&t->line_len[0], &t->line_len[1],
                (size_t)(TERM_MAX_LINES - 1) * sizeof(u16));
        memmove(&t->line_color[0], &t->line_color[1],
                (size_t)(TERM_MAX_LINES - 1));
    }

    line_reset(t, t->line_count - 1);
    t->scroll = 0;
}

void term_write_len(terminal_t *t, const char *text, int len, u32 color)
{
    if (!t || !text || len <= 0) return;

    int ci = (color < TERM_C_COUNT) ? (int)color : TERM_C_DEFAULT;
    int cur = t->line_count - 1;
    int col = t->line_len[cur];

    for (int i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)text[i];

        if (ch == '\n') {
            term_newline(t);
            cur = t->line_count - 1;
            col = t->line_len[cur];
            continue;
        }
        if (ch == '\r') {
            col = 0;
            t->line_len[cur] = 0;
            t->lines[cur][0] = '\0';
            continue;
        }
        if (ch < 32 || ch > 126) {
            if (ch == '\t') {
                int spaces = 4 - (col & 3);
                for (int s = 0; s < spaces; s++) {
                    if (col >= TERM_MAX_COLS - 1) {
                        term_newline(t);
                        cur = t->line_count - 1;
                        col = 0;
                    }
                    t->lines[cur][col++] = ' ';
                    t->line_color[cur] = (u8)ci;
                }
                t->lines[cur][col] = '\0';
                t->line_len[cur] = (u16)col;
                continue;
            }
            ch = '?';
        }

        if (col >= TERM_MAX_COLS - 1) {
            term_newline(t);
            cur = t->line_count - 1;
            col = 0;
        }

        t->lines[cur][col++] = (char)ch;
        t->line_color[cur] = (u8)ci;
        t->lines[cur][col] = '\0';
        t->line_len[cur] = (u16)col;
    }

    t->scroll = 0;
}

void term_write(terminal_t *t, const char *text, u32 color)
{
    if (!text) return;
    term_write_len(t, text, kstrlen(text), color);
}

void term_printf(terminal_t *t, u32 color, const char *fmt, ...)
{
    char buf[1024];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    term_write(t, buf, color);
}

void term_scroll(terminal_t *t, int delta)
{
    if (!t) return;
    t->scroll += delta;
    if (t->scroll < 0) t->scroll = 0;
    if (t->scroll > t->line_count - 1) t->scroll = t->line_count - 1;
}

/* ── Terminal window presentation ─────────────────────────────────────────── */

static u32 term_palette(int index)
{
    switch (index) {
    case TERM_C_ACCENT: return theme.accent;
    case TERM_C_OK:     return theme.success;
    case TERM_C_ERR:    return theme.danger;
    case TERM_C_WARN:   return theme.warn;
    case TERM_C_DIM:    return theme.text_faint;
    default:            return theme.term_text;
    }
}

static void term_prompt_text(terminal_t *t, char *buf, int cap)
{
    ksnprintf(buf, cap, "nova@kernel:%s$ ", t->cwd);
}

static void term_sync_title(terminal_t *t)
{
    if (!t || !t->win) return;
    char title[64];
    ksnprintf(title, sizeof(title), "Terminal - %s", t->cwd);
    wm_set_title(t->win, title);
}

static void term_draw(window_t *w)
{
    terminal_t *t = (terminal_t *)w->state;
    if (!t) return;

    int x = CX(w), y = CY(w);
    int cw = CWD(w), ch = CHT(w);

    gfx_fill(x, y, cw, ch, theme.term_bg);

    /* Header strip */
    int bar_h = 30;
    gfx_fill(x, y, cw, bar_h, theme.panel);
    gfx_fill(x, y + bar_h - 1, cw, 1, theme.border);
    gfx_fill(x + 10, y + 10, 4, 10, theme.accent);
    gfx_text(x + 22, y + 7, t->cwd, theme.accent);
    gfx_text_right(x + cw - 12, y + 7, "interactive shell", theme.text_faint);

    int area_y = y + bar_h + 6;
    int area_h = ch - bar_h - APP_STATUS_H - 8;
    if (area_h < 32) area_h = 32;

    int cols = max(20, (cw - 26) / 8);
    int rows = max(2, area_h / 18);
    int out_rows = rows - 1;

    int max_scroll = max(0, t->line_count - out_rows);
    if (t->scroll > max_scroll) t->scroll = max_scroll;
    if (t->scroll < 0) t->scroll = 0;

    int start = t->line_count - out_rows - t->scroll;
    if (start < 0) start = 0;

    for (int row = 0; row < out_rows; row++) {
        int li = start + row;
        if (li >= t->line_count) break;
        int ly = area_y + row * 18;
        if (t->line_len[li] > 0)
            gfx_text(x + 12, ly, t->lines[li], term_palette(t->line_color[li]));
    }

    /* Prompt + editable input on the last reserved row. */
    int py = area_y + out_rows * 18;
    char prompt[VFS_PATH_MAX + 24];
    term_prompt_text(t, prompt, sizeof(prompt));

    int plen = kstrlen(prompt);
    if (plen > cols - 8) {
        kstrcpy(prompt, "nova$ ");
        plen = 6;
    }

    gfx_text(x + 12, py, prompt, theme.accent);

    int avail = cols - plen;
    if (avail < 4) avail = 4;

    int istart = 0;
    if (t->input_cursor > avail - 1)
        istart = t->input_cursor - avail + 1;
    if (istart > t->input_len) istart = t->input_len;

    char visible[TERM_INPUT_MAX + 1];
    int vcount = t->input_len - istart;
    if (vcount > avail) vcount = avail;
    if (vcount < 0) vcount = 0;
    if (vcount > 0)
        memcpy(visible, t->input + istart, (size_t)vcount);
    visible[vcount] = '\0';
    gfx_text(x + 12 + plen * 8, py, visible, theme.term_text);

    if ((timer_ticks() / 50) % 2 == 0) {
        int caret_col = t->input_cursor - istart;
        if (caret_col < 0) caret_col = 0;
        if (caret_col <= avail)
            gfx_fill(x + 12 + (plen + caret_col) * 8, py, 2, 16, theme.accent);
    }

    /* Scrollbar when history overflows. */
    if (t->line_count > out_rows)
        ui_scrollbar(x + cw - 10, area_y, 6, out_rows * 18,
                     t->line_count - out_rows - t->scroll,
                     t->line_count, out_rows);

    /* Status bar */
    int sy = y + ch - APP_STATUS_H;
    gfx_fill(x, sy, cw, APP_STATUS_H, theme.panel);
    gfx_fill(x, sy, cw, 1, theme.border);
    gfx_text(x + 10, sy + 3, "Ctrl+L clear   Ctrl+C cancel   Tab complete",
             theme.text_faint);
    char info[64];
    ksnprintf(info, sizeof(info), "lines %d   scroll %d", t->line_count, t->scroll);
    gfx_text_right(x + cw - 10, sy + 3, info, theme.text_faint);
}

/* ── Input editing ────────────────────────────────────────────────────────── */

static void term_history_add(terminal_t *t, const char *line)
{
    if (!line || !*line) return;
    if (t->history_count > 0 &&
        kstrcmp(t->history[(t->history_count - 1) % TERM_HISTORY], line) == 0)
        return;

    kstrncpy(t->history[t->history_count % TERM_HISTORY], line, TERM_INPUT_MAX);
    if (t->history_count < TERM_HISTORY) t->history_count++;
    t->history_pos = t->history_count;
}

static void term_history_load(terminal_t *t, int pos)
{
    if (t->history_count <= 0) return;
    if (pos < 0) pos = 0;
    if (pos >= t->history_count) {
        t->history_pos = t->history_count;
        kstrncpy(t->input, t->saved_input, TERM_INPUT_MAX);
        t->input_len = kstrlen(t->input);
        t->input_cursor = t->input_len;
        return;
    }
    t->history_pos = pos;
    kstrncpy(t->input, t->history[pos], TERM_INPUT_MAX);
    t->input_len = kstrlen(t->input);
    t->input_cursor = t->input_len;
}

static void term_insert(terminal_t *t, char c)
{
    if (t->input_len >= TERM_INPUT_MAX - 1) return;
    for (int i = t->input_len; i > t->input_cursor; i--)
        t->input[i] = t->input[i - 1];
    t->input[t->input_cursor] = c;
    t->input_len++;
    t->input_cursor++;
    t->input[t->input_len] = '\0';
}

static void term_delete_back(terminal_t *t)
{
    if (t->input_cursor <= 0) return;
    for (int i = t->input_cursor - 1; i < t->input_len - 1; i++)
        t->input[i] = t->input[i + 1];
    t->input_cursor--;
    t->input_len--;
    t->input[t->input_len] = '\0';
}

static void term_delete_at(terminal_t *t)
{
    if (t->input_cursor >= t->input_len) return;
    for (int i = t->input_cursor; i < t->input_len - 1; i++)
        t->input[i] = t->input[i + 1];
    t->input_len--;
    t->input[t->input_len] = '\0';
}

static void term_complete(terminal_t *t)
{
    /* Complete the final whitespace-separated token against the VFS. */
    int start = t->input_len;
    while (start > 0 && t->input[start - 1] != ' ') start--;
    const char *token = t->input + start;
    if (!*token) return;

    char dirpath[VFS_PATH_MAX];
    const char *slash = kstrrchr(token, '/');
    const char *prefix = token;
    if (slash) {
        int dlen = (int)(slash - token);
        if (dlen == 0) {
            kstrcpy(dirpath, "/");
        } else {
            if (dlen >= (int)sizeof(dirpath)) return;
            memcpy(dirpath, token, (size_t)dlen);
            dirpath[dlen] = '\0';
        }
        prefix = slash + 1;
    } else {
        kstrcpy(dirpath, t->cwd);
    }

    int dir = vfs_lookup(dirpath, t->cwd);
    if (dir < 0 || !vfs_is_dir(dir)) return;

    int entries[VFS_MAX_NODES];
    int count = vfs_list(dir, entries, VFS_MAX_NODES);
    int plen = kstrlen(prefix);
    int matches = 0;
    int last = -1;

    for (int i = 0; i < count; i++) {
        const char *name = vfs_name(entries[i]);
        if (kstrncmp(name, prefix, plen) == 0) {
            matches++;
            last = entries[i];
            if (matches <= 8) {
                term_write(t, "  ", TERM_C_DIM);
                term_write(t, name, vfs_is_dir(entries[i]) ? TERM_C_ACCENT : TERM_C_DEFAULT);
            }
        }
    }

    if (matches == 0) return;

    if (matches == 1) {
        const char *name = vfs_name(last);
        int suffix_start = (int)(prefix - token);
        char completed[VFS_PATH_MAX + VFS_NAME_MAX];
        int n = 0;
        for (int i = 0; i < suffix_start && n < (int)sizeof(completed) - 2; i++)
            completed[n++] = token[i];
        for (int i = 0; name[i] && n < (int)sizeof(completed) - 2; i++)
            completed[n++] = name[i];
        if (vfs_is_dir(last)) completed[n++] = '/';
        completed[n] = '\0';

        int base = start;
        t->input_len = base;
        t->input_cursor = base;
        for (int i = 0; completed[i]; i++) term_insert(t, completed[i]);
        return;
    }

    term_newline(t);
    term_write(t, "matches: ", TERM_C_DIM);
    for (int i = 0; i < count; i++) {
        const char *name = vfs_name(entries[i]);
        if (kstrncmp(name, prefix, plen) == 0) {
            term_write(t, name, vfs_is_dir(entries[i]) ? TERM_C_ACCENT : TERM_C_DEFAULT);
            term_write(t, "  ", TERM_C_DIM);
        }
    }
    term_newline(t);
    t->scroll = 0;
}

static void term_submit(window_t *w, terminal_t *t)
{
    char line[TERM_INPUT_MAX];
    kstrncpy(line, t->input, sizeof(line));

    char prompt[VFS_PATH_MAX + 24];
    term_prompt_text(t, prompt, sizeof(prompt));
    term_write(t, prompt, TERM_C_ACCENT);
    term_write_len(t, t->input, t->input_len, TERM_C_DEFAULT);
    term_newline(t);

    term_history_add(t, line);
    t->input_len = 0;
    t->input_cursor = 0;
    t->input[0] = '\0';
    t->scroll = 0;

    shell_execute(t, line);
    term_sync_title(t);

    if (t->input_len < 0) {
        /* The `exit` command requested that the window closes. */
        wm_close(w);
    }
}



static void term_key(window_t *w, const key_event_t *ev)
{
    terminal_t *t = (terminal_t *)w->state;
    if (!t) return;

    /* Regular character input (no modifier) */
    if (ev->ch >= 32 && ev->ch <= 126) {
        term_insert(t, (char)ev->ch);
        return;
    }

    switch (ev->key) {
    case KEY_ENTER:
        term_submit(w, t);
        return;
    case KEY_BACKSPACE:
        term_delete_back(t);
        return;
    case KEY_DELETE:
        term_delete_at(t);
        return;
    case KEY_LEFT:
        if (t->input_cursor > 0) t->input_cursor--;
        return;
    case KEY_RIGHT:
        if (t->input_cursor < t->input_len) t->input_cursor++;
        return;
    case KEY_HOME:
        t->input_cursor = 0;
        return;
    case KEY_END:
        t->input_cursor = t->input_len;
        return;
    case KEY_UP:
        if (t->history_pos == t->history_count)
            kstrncpy(t->saved_input, t->input, TERM_INPUT_MAX);
        term_history_load(t, t->history_pos - 1);
        return;
    case KEY_DOWN:
        term_history_load(t, t->history_pos + 1);
        return;
    case KEY_TAB:
        term_complete(t);
        return;
    case KEY_PAGEUP:
        term_scroll(t, 8);
        return;
    case KEY_PAGEDOWN:
        term_scroll(t, -8);
        return;
    default:
        break;
    }

    if (ev->ch >= 32 && ev->ch <= 126) {
        term_insert(t, (char)ev->ch);
        return;
    }
}

static void term_wheel(window_t *w, int delta, int x, int y)
{
    terminal_t *t = (terminal_t *)w->state;
    UNUSED(x); UNUSED(y);
    if (!t) return;
    term_scroll(t, delta > 0 ? 2 : -2);
}

static void term_close(window_t *w)
{
    if (w->state) {
        kfree(w->state);
        w->state = NULL;
    }
}

/* ── Application entry points ─────────────────────────────────────────────── */

void app_terminal_open_at(const char *cwd)
{
    window_t *existing = wm_find(APP_TERMINAL);
    if (existing) {
        wm_focus(existing);
        if (cwd && *cwd) {
            terminal_t *t = (terminal_t *)existing->state;
            if (t) {
                int node = vfs_lookup(cwd, "/");
                if (node >= 0 && vfs_is_dir(node)) {
                    vfs_path(node, t->cwd, sizeof(t->cwd));
                    term_sync_title(t);
                }
            }
        }
        return;
    }

    terminal_t *t = (terminal_t *)kzalloc(sizeof(terminal_t));
    if (!t) {
        toast_show("Terminal", "Not enough memory to open the terminal", TOAST_ERROR);
        return;
    }
    term_init(t);

    if (cwd && *cwd) {
        int node = vfs_lookup(cwd, "/");
        if (node >= 0 && vfs_is_dir(node))
            vfs_path(node, t->cwd, sizeof(t->cwd));
    }

    int ww = min(880, max(520, gfx.width - 160));
    int wh = min(580, max(340, gfx.height - 170));
    int wx = max(20, (gfx.width - ww) / 2 + 18);
    int wy = max(50, (gfx.height - wh) / 2 - 10);

    window_t *w = wm_create(APP_TERMINAL, "Terminal", wx, wy, ww, wh,
                            460, 300, term_draw, term_key, NULL, term_wheel,
                            term_close, t);
    if (!w) {
        kfree(t);
        return;
    }

    t->win = w;
    shell_welcome(t);
    term_sync_title(t);
}

void app_terminal_open(void)
{
    app_terminal_open_at("/home/dev");
}

void app_terminal_execute(const char *command)
{
    app_terminal_open();
    window_t *w = wm_find(APP_TERMINAL);
    if (!w) return;
    terminal_t *t = (terminal_t *)w->state;
    if (!t || !command || !*command) return;

    char prompt[VFS_PATH_MAX + 24];
    term_prompt_text(t, prompt, sizeof(prompt));
    term_write(t, prompt, TERM_C_ACCENT);
    term_write(t, command, TERM_C_DEFAULT);
    term_newline(t);

    shell_execute(t, command);
    term_sync_title(t);

    if (t->input_len < 0) wm_close(w);
}
