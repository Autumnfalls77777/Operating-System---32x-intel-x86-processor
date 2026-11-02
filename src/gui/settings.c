/* ============================================================================
 * src/gui/settings.c  -  User preferences and /etc/nova.conf persistence
 * ============================================================================ */

#include "kernel.h"
#include "vfs.h"
#include "theme.h"
#include "settings.h"

settings_t g_settings;

const char *setting_accent_names[] = { "cyan", "violet", "emerald", "amber", "rose" };
const char *setting_wallpaper_names[] = { "aurora", "grid", "nebula", "slate" };
const int setting_accent_count = 5;
const int setting_wallpaper_count = 4;

static int name_index(const char *value, const char **names, int count)
{
    for (int i = 0; i < count; i++)
        if (kstrcasecmp(value, names[i]) == 0) return i;
    return -1;
}

void settings_init(void)
{
    g_settings.accent = 0;
    g_settings.wallpaper = 0;
    g_settings.animations = 1;
    g_settings.show_fps = 0;
    g_settings.dock_magnify = 1;
    g_settings.clock_seconds = 0;

    char buf[512];
    int n = vfs_read_file("/etc/nova.conf", "/", buf, sizeof(buf));
    if (n > 0) {
        char *line = buf;
        while (*line) {
            char *end = kstrchr(line, '\n');
            if (end) *end = '\0';
            char *eq = kstrchr(line, '=');
            if (eq) {
                *eq = '\0';
                char *key = line;
                char *val = eq + 1;
                kstrtrim(key);
                kstrtrim(val);
                if (kstrcmp(key, "accent") == 0) {
                    int idx = name_index(val, setting_accent_names, 5);
                    if (idx < 0) idx = katoi(val);
                    if (idx >= 0 && idx < 5) g_settings.accent = idx;
                } else if (kstrcmp(key, "wallpaper") == 0) {
                    int idx = name_index(val, setting_wallpaper_names, 4);
                    if (idx < 0) idx = katoi(val);
                    if (idx >= 0 && idx < 4) g_settings.wallpaper = idx;
                } else if (kstrcmp(key, "animations") == 0) {
                    g_settings.animations = katoi(val) != 0;
                } else if (kstrcmp(key, "show_fps") == 0) {
                    g_settings.show_fps = katoi(val) != 0;
                } else if (kstrcmp(key, "dock_magnify") == 0) {
                    g_settings.dock_magnify = katoi(val) != 0;
                } else if (kstrcmp(key, "clock_seconds") == 0) {
                    g_settings.clock_seconds = katoi(val) != 0;
                }
            }
            line = end ? end + 1 : line + kstrlen(line);
        }
    }

    theme_apply(g_settings.accent, g_settings.wallpaper);
}

void settings_apply(void)
{
    theme_apply(g_settings.accent, g_settings.wallpaper);
}

int settings_save(void)
{
    char buf[512];
    ksnprintf(buf, sizeof(buf),
              "# Altitude OS user preferences\n"
              "accent=%s\n"
              "wallpaper=%s\n"
              "animations=%d\n"
              "show_fps=%d\n"
              "dock_magnify=%d\n"
              "clock_seconds=%d\n",
              setting_accent_names[g_settings.accent],
              setting_wallpaper_names[g_settings.wallpaper],
              g_settings.animations, g_settings.show_fps,
              g_settings.dock_magnify, g_settings.clock_seconds);

    return vfs_write_file("/etc/nova.conf", "/", buf, (u32)kstrlen(buf), 0);
}
