/* ============================================================================
 * src/apps/apps_internal.h  -  Shared application helpers and entry points
 * ============================================================================ */

#ifndef NOVA_APPS_INTERNAL_H
#define NOVA_APPS_INTERNAL_H

#include "kernel.h"
#include "apps.h"
#include "window.h"
#include "gfx.h"

/* Absolute content rectangle for a window. */
#define CX(w)  ((w)->x + 1)
#define CY(w)  ((w)->y + WM_TITLE_H)
#define CWD(w) ((w)->w - 2)
#define CHT(w) ((w)->h - WM_TITLE_H - 1)

#define APP_BAR_H     36
#define APP_STATUS_H  22

/* Helpers implemented in src/apps/apps.c */
void app_bar(window_t *w, int height, const char *left, const char *right);
void app_status(window_t *w, const char *left, const char *right, u32 right_color);
int  app_hit(int mx, int my, int x, int y, int w, int h);

/* Module entry points. */
void app_terminal_open(void);
void app_terminal_open_at(const char *cwd);
void app_terminal_execute(const char *command);

void app_files_open(void);
void app_files_open_at(const char *path);

void app_editor_open(void);
void app_editor_load(const char *path);

void app_monitor_open(void);
void monitor_second(void);

void app_process_open(void);
void app_calculator_open(void);
void app_devtools_open(void);
void app_settings_open(void);
void app_about_open(void);
void app_help_open(void);

#endif /* NOVA_APPS_INTERNAL_H */
