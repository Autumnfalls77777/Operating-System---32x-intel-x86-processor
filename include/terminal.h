/* ============================================================================
 * include/terminal.h  -  Terminal state and output API
 * ============================================================================ */

#ifndef NOVA_TERMINAL_H
#define NOVA_TERMINAL_H

#include "types.h"
#include "vfs.h"
#include "window.h"

#define TERM_MAX_LINES 240
#define TERM_MAX_COLS  120
#define TERM_INPUT_MAX 180
#define TERM_HISTORY   32

/* Colour indices stored with each terminal line. */
#define TERM_C_DEFAULT 0
#define TERM_C_ACCENT  1
#define TERM_C_OK      2
#define TERM_C_ERR     3
#define TERM_C_WARN    4
#define TERM_C_DIM     5
#define TERM_C_COUNT   6

typedef struct {
    char lines[TERM_MAX_LINES][TERM_MAX_COLS];
    u16  line_len[TERM_MAX_LINES];
    u8   line_color[TERM_MAX_LINES];
    int  line_count;
    int  scroll;                 /* lines scrolled up from bottom        */
    char input[TERM_INPUT_MAX];
    int  input_len;
    int  input_cursor;
    char cwd[VFS_PATH_MAX];
    char history[TERM_HISTORY][TERM_INPUT_MAX];
    int  history_count;
    int  history_pos;
    char saved_input[TERM_INPUT_MAX];
    int  script_depth;
    window_t *win;
} terminal_t;

void term_init(terminal_t *t);
void term_clear(terminal_t *t);
void term_write(terminal_t *t, const char *text, u32 color);
void term_write_len(terminal_t *t, const char *text, int len, u32 color);
void term_printf(terminal_t *t, u32 color, const char *fmt, ...);
void term_newline(terminal_t *t);
void term_scroll(terminal_t *t, int delta);

#endif /* NOVA_TERMINAL_H */
