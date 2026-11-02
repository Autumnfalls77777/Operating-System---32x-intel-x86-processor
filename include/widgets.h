/* ============================================================================
 * include/widgets.h  -  Reusable GUI widget drawing helpers
 * ============================================================================ */

#ifndef NOVA_WIDGETS_H
#define NOVA_WIDGETS_H

#include "types.h"

int  ui_hit(int mx, int my, int x, int y, int w, int h);

void ui_panel(int x, int y, int w, int h, int radius, u32 fill, u32 border);
void ui_card(int x, int y, int w, int h, u32 fill, u32 border);
void ui_button(int x, int y, int w, int h, const char *label, int hover, int primary);
void ui_button_small(int x, int y, int w, int h, const char *label, int hover, u32 color);
void ui_toolbar_button(int x, int y, int w, int h, const char *label, int hover);
void ui_progress(int x, int y, int w, int h, int value, int max, u32 color);
void ui_bar(int x, int y, int w, int h, int value, int max, u32 color, u32 track);
void ui_pill(int x, int y, const char *label, u32 fg, u32 bg);
void ui_badge(int x, int y, const char *label, u32 color);
void ui_divider(int x, int y, int w);
void ui_keycap(int x, int y, const char *label);
void ui_text_input(int x, int y, int w, int h, const char *text, int cursor, int focused, const char *placeholder);
void ui_toggle(int x, int y, int w, int h, int on, const char *label);
void ui_slider(int x, int y, int w, int h, int value, int max, u32 color);
void ui_scrollbar(int x, int y, int w, int h, int pos, int total, int visible);
void ui_checkbox(int x, int y, int checked, const char *label);
void ui_tooltip(int x, int y, const char *text);

#endif /* NOVA_WIDGETS_H */
