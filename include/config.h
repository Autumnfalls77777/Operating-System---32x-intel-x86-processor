/* ============================================================================
 * include/config.h  -  Global build configuration and physical memory map
 * ============================================================================ */

#ifndef NOVA_CONFIG_H
#define NOVA_CONFIG_H

#define NOVA_VERSION   "1.0.0"
#define NOVA_CODENAME  "Aurora"
#define NOVA_BUILD     "2026.11"

/* ── Kernel physical memory layout ─────────────────────────────────────────
 *
 *   0x00005000  boot_info_t supplied by the stage-2 bootloader
 *   0x00008000  stage-2 bootloader (re-used as stack scratch after boot)
 *   0x00010000  kernel image (.text/.rodata/.data/.bss)
 *   0x0009F000  initial kernel stack top (grows downward)
 *   0x01000000  wallpaper back-store   (16 MiB window, max 1920x1080x4)
 *   0x02000000  compositor back-buffer (16 MiB window, max 1920x1080x4)
 *   0x04000000  kernel heap            (32 MiB)
 */
#define BOOT_INFO_ADDR     0x00005000u
#define KERNEL_BASE        0x00010000u
#define KERNEL_STACK_TOP   0x0009F000u
#define WALLPAPER_ADDR     0x01000000u
#define BACKBUFFER_ADDR    0x02000000u
#define HEAP_START         0x04000000u
#define HEAP_SIZE          (32u * 1024u * 1024u)

/* Maximum supported framebuffer resolution (one 16 MiB staging window). */
#define MAX_SCREEN_W       1920
#define MAX_SCREEN_H       1080

/* Timer frequency (PIT channel 0). */
#define TIMER_HZ           100

#endif /* NOVA_CONFIG_H */
