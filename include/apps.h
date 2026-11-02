/* ============================================================================
 * include/apps.h  -  Application registry and launch API
 * ============================================================================ */

#ifndef NOVA_APPS_H
#define NOVA_APPS_H

#include "types.h"
#include "window.h"

typedef enum {
    APP_TERMINAL = 0,
    APP_FILES,
    APP_EDITOR,
    APP_MONITOR,
    APP_PROCESS,
    APP_CALCULATOR,
    APP_DEVTOOLS,
    APP_SETTINGS,
    APP_ABOUT,
    APP_HELP,
    APP_COUNT
} app_id_t;

typedef struct {
    const char *name;
    const char *short_name;
    const char *description;
    u32         color;
} app_info_t;

const app_info_t *app_info(app_id_t id);
int               app_id_from_name(const char *name);
void              app_open(app_id_t id);
void              app_open_named(const char *name);
void              app_icon_draw(app_id_t id, int x, int y, int size, int active);
void              app_icon_draw_plain(app_id_t id, int x, int y, int size, u32 color);

/* Cross-app actions */
void app_editor_open_file(const char *path);
void app_files_open_path(const char *path);
void app_terminal_run(const char *command);
void app_terminal_open_with(const char *cwd);

/* Periodic hooks called from the compositor loop */
void apps_on_tick(u32 ticks);
int  apps_auto_tick_due(void);
void apps_on_second(void);

/* Scheduler demo used by the Process Manager */
void app_set_auto_sched(int on);
int  app_get_auto_sched(void);

/* Power actions with a confirmation toast */
void app_power_reboot(void);
void app_power_halt(void);

#endif /* NOVA_APPS_H */
