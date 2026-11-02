/* ============================================================================
 * src/apps/calculator.c  -  Integer expression calculator
 *
 * A small recursive-descent parser keeps the calculator useful for
 * programmers: precedence, parentheses, unary minus, power and modulo are all
 * supported without pulling floating point into the kernel.
 * ============================================================================ */

#include "kernel.h"
#include "theme.h"
#include "gfx.h"
#include "widgets.h"
#include "mouse.h"
#include "timer.h"
#include "memory.h"
#include "apps.h"
#include "desktop.h"
#include "apps_internal.h"

#define CALC_BAR_H     40
#define CALC_DISPLAY_H 86
#define CALC_COLS      5
#define CALC_ROWS      4
#define CALC_HISTORY   6
#define CALC_INPUT_MAX 96

typedef struct {
    char input[CALC_INPUT_MAX];
    int  len;
    int  cursor;
    int  result;
    int  has_result;
    int  fresh_result;
    char error[64];
    char history[CALC_HISTORY][CALC_INPUT_MAX + 24];
    int  history_count;
} calc_state_t;

static const char *calc_labels[CALC_ROWS * CALC_COLS] = {
    "C", "(", ")", "Del", "%",
    "7", "8", "9", "/",   "*",
    "4", "5", "6", "-",   "+",
    "1", "2", "3", "0",   "="
};

/* ── Expression parser ───────────────────────────────────────────────────── */

typedef struct {
    const char *p;
    int ok;
} calc_parser_t;

static void cp_skip(calc_parser_t *c)
{
    while (*c->p == ' ' || *c->p == '\t') c->p++;
}

static int cp_expr(calc_parser_t *c);

static int cp_primary(calc_parser_t *c)
{
    cp_skip(c);
    if (!c->ok) return 0;

    if (*c->p == '(') {
        c->p++;
        int v = cp_expr(c);
        cp_skip(c);
        if (*c->p != ')') {
            c->ok = 0;
            return 0;
        }
        c->p++;
        return v;
    }

    if (*c->p < '0' || *c->p > '9') {
        c->ok = 0;
        return 0;
    }

    int v = 0;
    while (*c->p >= '0' && *c->p <= '9') {
        v = v * 10 + (*c->p - '0');
        c->p++;
    }
    return v;
}

static int cp_unary(calc_parser_t *c);

static int cp_power(calc_parser_t *c)
{
    int base = cp_primary(c);
    cp_skip(c);
    if (*c->p == '^') {
        c->p++;
        int exp = cp_unary(c);
        if (exp < 0) {
            /* Integer-only: negative exponents collapse to zero unless the
             * base is 1 or -1. */
            if (base == 1) return 1;
            if (base == -1) return (exp & 1) ? -1 : 1;
            return 0;
        }
        int result = 1;
        while (exp-- > 0) result *= base;
        return result;
    }
    return base;
}

static int cp_unary(calc_parser_t *c)
{
    cp_skip(c);
    if (*c->p == '-') {
        c->p++;
        return -cp_unary(c);
    }
    if (*c->p == '+') {
        c->p++;
        return cp_unary(c);
    }
    return cp_power(c);
}

static int cp_term(calc_parser_t *c)
{
    int v = cp_unary(c);
    for (;;) {
        cp_skip(c);
        char op = *c->p;
        if (op != '*' && op != '/' && op != '%') break;
        c->p++;
        int rhs = cp_unary(c);
        if (!c->ok) return 0;
        if (op == '*') {
            v *= rhs;
        } else if (op == '/') {
            if (rhs == 0) {
                c->ok = 0;
                return 0;
            }
            v /= rhs;
        } else {
            if (rhs == 0) {
                c->ok = 0;
                return 0;
            }
            v %= rhs;
        }
    }
    return v;
}

static int cp_expr(calc_parser_t *c)
{
    int v = cp_term(c);
    for (;;) {
        cp_skip(c);
        char op = *c->p;
        if (op != '+' && op != '-') break;
        c->p++;
        int rhs = cp_term(c);
        if (!c->ok) return 0;
        if (op == '+') v += rhs;
        else           v -= rhs;
    }
    return v;
}

static int calc_parse(const char *text, int *out, char *error, int cap)
{
    calc_parser_t c;
    c.p = text;
    c.ok = 1;

    cp_skip(&c);
    if (!*c.p) {
        kstrcpy(error, "Nothing to evaluate");
        return -1;
    }

    int value = cp_expr(&c);
    cp_skip(&c);

    if (!c.ok || *c.p) {
        kstrcpy(error, c.ok ? "Unexpected character" : "Invalid expression");
        return -1;
    }

    *out = value;
    error[0] = '\0';
    (void)cap;
    return 0;
}

/* ── State helpers ────────────────────────────────────────────────────────── */

static void calc_clear(calc_state_t *c)
{
    c->input[0] = '\0';
    c->len = 0;
    c->cursor = 0;
    c->has_result = 0;
    c->fresh_result = 0;
    c->error[0] = '\0';
}

static void calc_set_input_from_result(calc_state_t *c)
{
    c->len = ksnprintf(c->input, CALC_INPUT_MAX, "%d", c->result);
    if (c->len < 0) c->len = 0;
    if (c->len >= CALC_INPUT_MAX) c->len = CALC_INPUT_MAX - 1;
    c->cursor = c->len;
}

static void calc_insert(calc_state_t *c, char ch)
{
    if (c->len >= CALC_INPUT_MAX - 1) return;
    if (c->cursor < 0) c->cursor = 0;
    if (c->cursor > c->len) c->cursor = c->len;

    for (int i = c->len; i > c->cursor; i--)
        c->input[i] = c->input[i - 1];

    c->input[c->cursor] = ch;
    c->len++;
    c->cursor++;
    c->input[c->len] = '\0';
    c->has_result = 0;
    c->fresh_result = 0;
    c->error[0] = '\0';
}

static void calc_backspace(calc_state_t *c)
{
    if (c->cursor <= 0) return;
    for (int i = c->cursor - 1; i < c->len - 1; i++)
        c->input[i] = c->input[i + 1];
    c->cursor--;
    c->len--;
    c->input[c->len] = '\0';
    c->has_result = 0;
    c->fresh_result = 0;
    c->error[0] = '\0';
}

static void calc_history_add(calc_state_t *c, const char *expr, int value)
{
    if (c->history_count < CALC_HISTORY) c->history_count++;
    int slot = (c->history_count - 1) % CALC_HISTORY;
    ksnprintf(c->history[slot], sizeof(c->history[slot]), "%s = %d", expr, value);
}

static void calc_evaluate(calc_state_t *c)
{
    if (c->len == 0) {
        kstrcpy(c->error, "Type an expression first");
        c->has_result = 0;
        return;
    }

    int value = 0;
    char error[64];
    error[0] = '\0';

    if (calc_parse(c->input, &value, error, sizeof(error)) < 0) {
        kstrncpy(c->error, error, sizeof(c->error));
        c->has_result = 0;
        c->fresh_result = 0;
        return;
    }

    c->result = value;
    c->has_result = 1;
    c->fresh_result = 1;
    c->error[0] = '\0';
    calc_history_add(c, c->input, value);
}

static void calc_press(calc_state_t *c, const char *label)
{
    if (kstrcmp(label, "C") == 0) {
        calc_clear(c);
        return;
    }
    if (kstrcmp(label, "Del") == 0) {
        calc_backspace(c);
        return;
    }
    if (kstrcmp(label, "=") == 0) {
        calc_evaluate(c);
        return;
    }

    /* After a result, a digit starts a new calculation while an operator
     * continues from the result. */
    if (c->fresh_result) {
        char first = label[0];
        if (first >= '0' && first <= '9') {
            calc_clear(c);
        } else if (first != '(') {
            calc_set_input_from_result(c);
            c->fresh_result = 0;
        } else {
            calc_clear(c);
        }
    }

    for (int i = 0; label[i]; i++) calc_insert(c, label[i]);
}

/* ── Layout ───────────────────────────────────────────────────────────────── */

static void calc_button_rect(window_t *w, int index, int *bx, int *by,
                             int *bw, int *bh)
{
    int cw = CWD(w);
    int ch = CHT(w);
    int pad = 14;
    int gap = 8;
    int top = CALC_BAR_H + CALC_DISPLAY_H + 10;

    int total_w = cw - pad * 2;
    int total_h = ch - top - APP_STATUS_H - 10;
    int bw_local = (total_w - gap * (CALC_COLS - 1)) / CALC_COLS;
    int bh_local = (total_h - gap * (CALC_ROWS - 1)) / CALC_ROWS;

    if (bw_local < 34) bw_local = 34;
    if (bh_local < 30) bh_local = 30;

    int col = index % CALC_COLS;
    int row = index / CALC_COLS;

    if (bx) *bx = pad + col * (bw_local + gap);
    if (by) *by = top + row * (bh_local + gap);
    if (bw) *bw = bw_local;
    if (bh) *bh = bh_local;
}

static u32 calc_button_color(const char *label, int hot)
{
    char c = label[0];

    if (kstrcmp(label, "=") == 0)
        return hot ? gfx_lerp(theme.accent, 0xFFFFFF, 28) : theme.accent;

    if (kstrcmp(label, "C") == 0)
        return hot ? gfx_lerp(theme.danger, 0x000000, 20)
                   : gfx_lerp(theme.panel2, theme.danger, 38);

    if (kstrcmp(label, "Del") == 0)
        return hot ? gfx_lerp(theme.warn, 0x000000, 24)
                   : gfx_lerp(theme.panel2, theme.warn, 26);

    if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '^')
        return hot ? gfx_lerp(theme.accent_soft, 0xFFFFFF, 20) : theme.accent_soft;

    if (c == '(' || c == ')')
        return hot ? gfx_lerp(theme.bg1, 0xFFFFFF, 18) : theme.bg1;

    return hot ? gfx_lerp(theme.panel2, 0xFFFFFF, 18) : theme.panel2;
}

/* ── Drawing ──────────────────────────────────────────────────────────────── */

static void calc_draw(window_t *w)
{
    calc_state_t *c = (calc_state_t *)w->state;
    if (!c) return;

    int x = CX(w), y = CY(w), cw = CWD(w), ch = CHT(w);
    gfx_fill(x, y, cw, ch, theme.win);
    app_bar(w, CALC_BAR_H, "Calculator", "integer mode");

    mouse_state_t *m = mouse_get();
    int mx = m ? m->x : -1;
    int my = m ? m->y : -1;

    /* Display card. */
    int dx = x + 14;
    int dy = y + CALC_BAR_H + 10;
    int dw = cw - 28;
    int dh = CALC_DISPLAY_H - 20;

    gfx_round_rect(dx, dy, dw, dh, 12, theme.term_bg);
    gfx_round_outline(dx, dy, dw, dh, 12,
                      c->error[0] ? theme.danger : theme.border);
    gfx_fill(dx + 12, dy + 12, 3, dh - 24, theme.accent);

    gfx_set_clip(dx + 22, dy, dw - 34, dh);
    if (c->len > 0)
        gfx_text(dx + 22, dy + 12, c->input, theme.text_dim);
    else
        gfx_text(dx + 22, dy + 12, "0", theme.text_faint);
    gfx_reset_clip();

    if (c->error[0]) {
        gfx_set_clip(dx + 22, dy + 36, dw - 34, 28);
        gfx_text(dx + 22, dy + 42, c->error, theme.danger);
        gfx_reset_clip();
    } else if (c->has_result) {
        char value[32];
        ksnprintf(value, sizeof(value), "= %d", c->result);
        int vw = gfx_text_width_scale(value, 2);
        int vx = dx + dw - 18 - vw;
        if (vx < dx + 20) vx = dx + 20;
        gfx_text_scale(vx, dy + 36, value, theme.accent, 2);
    } else {
        gfx_text(dx + 22, dy + 42, "supports + - * / % ^ ( )", theme.text_faint);
    }

    /* Recent history on the right of the status area if there is room. */
    const char *latest = NULL;
    if (c->history_count > 0)
        latest = c->history[(c->history_count - 1) % CALC_HISTORY];

    /* Keys. */
    for (int i = 0; i < CALC_ROWS * CALC_COLS; i++) {
        int bx, by, bw, bh;
        calc_button_rect(w, i, &bx, &by, &bw, &bh);
        int ax = x + bx;
        int ay = y + by;
        const char *label = calc_labels[i];
        int hot = app_hit(mx, my, ax, ay, bw, bh);

        u32 base = calc_button_color(label, hot);
        u32 fg = (kstrcmp(label, "=") == 0) ? 0x06101F : theme.text;

        gfx_round_rect(ax, ay, bw, bh, 9, base);
        if (hot)
            gfx_round_outline(ax, ay, bw, bh, 9,
                              gfx_lerp(base, 0xFFFFFF, 70));
        else
            gfx_round_outline(ax, ay, bw, bh, 9, theme.border);

        int tw = gfx_text_width(label);
        gfx_text(ax + (bw - tw) / 2, ay + (bh - 16) / 2, label, fg);
    }

    /* Status bar. */
    char left[160];
    ksnprintf(left, sizeof(left), "Enter evaluate  |  Backspace delete  |  Esc clear");
    char right[128];
    if (latest) {
        /* Keep the newest line short so it never collides with the hints. */
        kstrncpy(right, latest, 48);
        right[48] = '\0';
    } else {
        kstrcpy(right, "history empty");
    }
    app_status(w, left, right, latest ? theme.accent2 : theme.text_faint);
}

/* ── Input ────────────────────────────────────────────────────────────────── */

static void calc_mouse(window_t *w, int mx, int my, int button, int pressed)
{
    calc_state_t *c = (calc_state_t *)w->state;
    if (!c || button != MOUSE_BTN_LEFT || !pressed) return;

    for (int i = 0; i < CALC_ROWS * CALC_COLS; i++) {
        int bx, by, bw, bh;
        calc_button_rect(w, i, &bx, &by, &bw, &bh);
        if (app_hit(mx, my, bx, by, bw, bh)) {
            calc_press(c, calc_labels[i]);
            return;
        }
    }
}

static int calc_is_input_char(char ch)
{
    if (ch >= '0' && ch <= '9') return 1;
    switch (ch) {
    case '+': case '-': case '*': case '/': case '%':
    case '^': case '(': case ')': case ' ':
        return 1;
    default:
        return 0;
    }
}

static void calc_key(window_t *w, const key_event_t *ev)
{
    calc_state_t *c = (calc_state_t *)w->state;
    if (!c) return;

    switch (ev->key) {
    case KEY_ENTER:
        calc_evaluate(c);
        return;
    case KEY_BACKSPACE:
        calc_backspace(c);
        return;
    case KEY_ESC:
        calc_clear(c);
        return;
    case KEY_LEFT:
        if (c->cursor > 0) c->cursor--;
        return;
    case KEY_RIGHT:
        if (c->cursor < c->len) c->cursor++;
        return;
    case KEY_HOME:
        c->cursor = 0;
        return;
    case KEY_END:
        c->cursor = c->len;
        return;
    default:
        break;
    }

    if (ev->ch && calc_is_input_char((char)ev->ch)) {
        if (c->fresh_result) {
            char ch = (char)ev->ch;
            if (ch >= '0' && ch <= '9') calc_clear(c);
            else if (ch != '(') {
                calc_set_input_from_result(c);
                c->fresh_result = 0;
            } else {
                calc_clear(c);
            }
        }
        calc_insert(c, (char)ev->ch);
    }
}

static void calc_close(window_t *w)
{
    if (w->state) {
        kfree(w->state);
        w->state = NULL;
    }
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

void app_calculator_open(void)
{
    window_t *w = wm_find(APP_CALCULATOR);
    if (w) {
        wm_focus(w);
        return;
    }

    calc_state_t *c = (calc_state_t *)kzalloc(sizeof(calc_state_t));
    if (!c) {
        toast_show("Calculator", "Not enough memory", TOAST_ERROR);
        return;
    }
    calc_clear(c);

    int ww = min(460, max(340, gfx.width / 3));
    int wh = min(560, max(430, gfx.height - 190));
    int wx = max(40, (gfx.width - ww) / 2 + 40);
    int wy = max(60, (gfx.height - wh) / 2 - 6);

    window_t *win = wm_create(APP_CALCULATOR, "Calculator", wx, wy, ww, wh,
                              340, 400, calc_draw, calc_key, calc_mouse,
                              NULL, calc_close, c);
    if (!win) kfree(c);
}
