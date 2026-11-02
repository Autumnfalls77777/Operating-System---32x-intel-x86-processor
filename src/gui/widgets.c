/* ============================================================================
 * src/gui/widgets.c  -  Reusable GUI widgets
 * ============================================================================ */

#include "kernel.h"
#include "gfx.h"
#include "theme.h"
#include "widgets.h"

int ui_hit(int mx, int my, int x, int y, int w, int h)
{
    return mx >= x && my >= y && mx < x + w && my < y + h;
}

void ui_panel(int x, int y, int w, int h, int radius, u32 fill, u32 border)
{
    gfx_shadow(x, y, w, h, radius, 10, 70);
    gfx_round_rect(x, y, w, h, radius, fill);
    if (border) gfx_round_outline(x, y, w, h, radius, border);
}

void ui_card(int x, int y, int w, int h, u32 fill, u32 border)
{
    gfx_round_rect(x, y, w, h, 10, fill);
    gfx_round_outline(x, y, w, h, 10, border);
}

void ui_button(int x, int y, int w, int h, const char *label, int hover, int primary)
{
    u32 base = primary ? theme.accent : theme.panel2;
    u32 border = primary ? theme.accent : theme.border;
    if (hover) base = gfx_lerp(base, 0xFFFFFF, primary ? 34 : 22);
    gfx_round_rect(x, y, w, h, 8, base);
    gfx_round_outline(x, y, w, h, 8, border);
    u32 fg = primary ? 0x06101F : theme.text;
    gfx_text(x + (w - gfx_text_width(label)) / 2,
             y + (h - 16) / 2, label, fg);
}

void ui_button_small(int x, int y, int w, int h, const char *label, int hover, u32 color)
{
    u32 base = hover ? gfx_lerp(color, 0xFFFFFF, 30) : color;
    gfx_round_rect(x, y, w, h, 6, base);
    gfx_text(x + (w - gfx_text_width(label)) / 2,
             y + (h - 16) / 2, label, 0x06101F);
}

void ui_toolbar_button(int x, int y, int w, int h, const char *label, int hover)
{
    ui_button(x, y, w, h, label, hover, 0);
}

void ui_progress(int x, int y, int w, int h, int value, int max, u32 color)
{
    if (max <= 0) max = 1;
    if (value < 0) value = 0;
    if (value > max) value = max;
    gfx_round_rect(x, y, w, h, h / 2, theme.bg1);
    int fill = (w * value) / max;
    if (fill > 0) gfx_round_rect(x, y, fill, h, h / 2, color);
}

void ui_bar(int x, int y, int w, int h, int value, int max, u32 color, u32 track)
{
    if (max <= 0) max = 1;
    value = clamp(value, 0, max);
    gfx_round_rect(x, y, w, h, h / 2, track);
    int fill = (w * value) / max;
    if (fill > 0) gfx_round_rect(x, y, fill, h, h / 2, color);
}

void ui_pill(int x, int y, const char *label, u32 fg, u32 bg)
{
    int w = gfx_text_width(label) + 16;
    gfx_round_rect(x, y, w, 20, 10, bg);
    gfx_text(x + 8, y + 2, label, fg);
}

void ui_badge(int x, int y, const char *label, u32 color)
{
    gfx_circle_fill(x + 4, y + 8, 4, color);
    gfx_text(x + 14, y, label, theme.text_dim);
}

void ui_divider(int x, int y, int w)
{
    gfx_fill(x, y, w, 1, theme.border);
}

void ui_keycap(int x, int y, const char *label)
{
    int w = gfx_text_width(label) + 12;
    gfx_round_rect(x, y, w, 20, 5, theme.panel2);
    gfx_round_outline(x, y, w, 20, 5, theme.border);
    gfx_text(x + 6, y + 2, label, theme.text_dim);
}

void ui_text_input(int x, int y, int w, int h, const char *text, int cursor,
                   int focused, const char *placeholder)
{
    gfx_round_rect(x, y, w, h, 8, theme.bg0);
    gfx_round_outline(x, y, w, h, 8, focused ? theme.accent : theme.border);

    int tx = x + 10;
    int ty = y + (h - 16) / 2;
    if (text && *text) {
        gfx_text(tx, ty, text, theme.text);
    } else if (placeholder) {
        gfx_text(tx, ty, placeholder, theme.text_faint);
    }

    if (focused) {
        int cxp = tx + cursor * 8;
        gfx_fill(cxp, ty, 2, 16, theme.accent);
    }
}

void ui_toggle(int x, int y, int w, int h, int on, const char *label)
{
    gfx_round_rect(x, y, w, h, h / 2, on ? theme.accent : theme.panel2);
    int knob = h - 6;
    int kx = on ? x + w - knob - 3 : x + 3;
    gfx_circle_fill(kx + knob / 2, y + h / 2, knob / 2, on ? 0x06101F : theme.text_dim);
    if (label) gfx_text(x + w + 12, y + (h - 16) / 2, label, theme.text_dim);
}

void ui_slider(int x, int y, int w, int h, int value, int max, u32 color)
{
    if (max <= 0) max = 1;
    value = clamp(value, 0, max);
    int cy = y + h / 2;
    gfx_fill(x, cy - 2, w, 4, theme.panel2);
    int fx = (w * value) / max;
    gfx_fill(x, cy - 2, fx, 4, color);
    gfx_circle_fill(x + fx, cy, 6, color);
}

void ui_scrollbar(int x, int y, int w, int h, int pos, int total, int visible)
{
    if (total <= visible || total <= 0) return;
    gfx_round_rect(x, y, w, h, w / 2, theme.bg1);
    int thumb_h = max(24, (h * visible) / total);
    int max_pos = total - visible;
    int thumb_y = y + ((h - thumb_h) * clamp(pos, 0, max_pos)) / max(1, max_pos);
    gfx_round_rect(x, thumb_y, w, thumb_h, w / 2, theme.text_faint);
}

void ui_checkbox(int x, int y, int checked, const char *label)
{
    gfx_round_rect(x, y, 16, 16, 4, checked ? theme.accent : theme.bg0);
    gfx_round_outline(x, y, 16, 16, 4, checked ? theme.accent : theme.border);
    if (checked) {
        gfx_line(x + 4, y + 8, x + 7, y + 11, 0x06101F);
        gfx_line(x + 7, y + 11, x + 13, y + 4, 0x06101F);
    }
    if (label) gfx_text(x + 24, y, label, theme.text_dim);
}

void ui_tooltip(int x, int y, const char *text)
{
    int w = gfx_text_width(text) + 20;
    int h = 28;
    if (x + w > gfx.width) x = gfx.width - w - 6;
    if (y < 0) y = 2;
    gfx_round_rect(x, y, w, h, 8, 0x020617);
    gfx_round_outline(x, y, w, h, 8, theme.border);
    gfx_text(x + 10, y + 6, text, theme.text);
}
