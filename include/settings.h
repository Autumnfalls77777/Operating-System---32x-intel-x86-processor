/* ============================================================================
 * include/settings.h  -  User preferences (in-memory, persisted to VFS)
 * ============================================================================ */

#ifndef NOVA_SETTINGS_H
#define NOVA_SETTINGS_H

#include "types.h"

typedef struct {
    int accent;        /* index into theme accent palette    */
    int wallpaper;     /* index into wallpaper list          */
    int animations;    /* 0/1                               */
    int show_fps;      /* 0/1                               */
    int dock_magnify;  /* 0/1                               */
    int clock_seconds; /* show seconds in the top bar        */
} settings_t;

extern settings_t g_settings;

void settings_init(void);
void settings_apply(void);
int  settings_save(void);

extern const char *setting_accent_names[];
extern const char *setting_wallpaper_names[];
extern const int   setting_accent_count;
extern const int   setting_wallpaper_count;

#endif /* NOVA_SETTINGS_H */
