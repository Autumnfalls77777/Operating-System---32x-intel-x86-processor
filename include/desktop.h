/* ============================================================================
 * include/desktop.h  -  Desktop compositor, panel, dock and overlays
 * ============================================================================ */

#ifndef NOVA_DESKTOP_H
#define NOVA_DESKTOP_H

#include "types.h"

#define TOAST_INFO    0
#define TOAST_SUCCESS 1
#define TOAST_WARN    2
#define TOAST_ERROR   3

void desktop_init(void);
void desktop_run(void) NORETURN;
void desktop_invalidate(void);
void desktop_workspace(int *x, int *y, int *w, int *h);
int  desktop_fps(void);
u32  desktop_frame_count(void);

void toast_show(const char *title, const char *msg, int kind);
void desktop_cursor_show(void);
void desktop_cursor_hide(void);

#endif /* NOVA_DESKTOP_H */
