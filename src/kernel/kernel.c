/* ============================================================================
 * src/kernel/kernel.c  -  Altitude OS boot orchestration
 *
 * Stage 2 hands us a boot_info_t describing the VBE framebuffer and the E820
 * memory map.  From there we bring up the graphics layer, interrupts, heap,
 * filesystem, scheduler, input drivers and finally the desktop compositor.
 * ============================================================================ */

#include "kernel.h"
#include "bootinfo.h"
#include "gfx.h"
#include "splash.h"
#include "theme.h"
#include "settings.h"
#include "desktop.h"
#include "window.h"
#include "apps.h"
#include "memory.h"
#include "vfs.h"
#include "procfs.h"
#include "process.h"
#include "interrupts.h"
#include "syscall.h"
#include "serial.h"
#include "timer.h"
#include "keyboard.h"
#include "mouse.h"
#include "rtc.h"
#include "cpu.h"
#include "pci.h"
#include "system.h"

#define SPLASH_TOTAL 10

static void splash_step(int index, const char *text)
{
    splash_stage(index, SPLASH_TOTAL, text, 1);
}

static void seed_processes(void)
{
    proc_spawn("compositor", 1, 240);
    proc_spawn("shell",      2, 160);
    proc_spawn("kworker",    4, 120);
    proc_spawn("logger",     6, 90);
}

void kernel_main(boot_info_t *bi)
{
    serial_init();
    klog_init();

    serial_write("\n");
    serial_write("========================================\n");
    serial_write("  Altitude OS " NOVA_VERSION " \"" NOVA_CODENAME "\"\n");
    serial_write("  32-bit programmer operating system\n");
    serial_write("========================================\n\n");

    g_boot_info = bi;

    if (!bi || bi->magic != BOOTINFO_MAGIC) {
        klog(KLOG_LEVEL_ERR, "missing or invalid boot information");
        serial_write("Boot information was not supplied by stage 2.\n");
        halt_machine();
    }

    klog(KLOG_LEVEL_INFO, "boot: framebuffer 0x%08x %ux%u %ubpp pitch %u",
         bi->fb_addr, bi->width, bi->height, bi->bpp, bi->pitch);
    klog(KLOG_LEVEL_INFO, "boot: lower=%u KiB upper=%u KiB e820=%u entries",
         bi->mem_lower_kb, bi->mem_upper_kb, bi->e820_count);

    /* â”€â”€ Graphics â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    int gerr = gfx_init(bi);
    if (gerr < 0) {
        serial_printf("gfx_init failed (%d); no usable framebuffer\n", gerr);
        halt_machine();
    }

    /* â”€â”€ Interrupt controller and clock â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    idt_init();
    pic_init();
    timer_init();
    pic_unmask(0);
    cpu_sti();

    splash_begin(bi);

    /* 1. Framebuffer */
    splash_step(0, "Graphics: VBE linear framebuffer online");

    /* 2. Heap */
    mem_init(HEAP_START, HEAP_SIZE);
    splash_step(1, "Memory: 32 MiB kernel heap at 0x04000000");

    /* 3. Interrupts */
    splash_step(2, "Interrupts: IDT, 8259 PIC and PIT at 100 Hz");

    /* 4. Input */
    keyboard_init();
    mouse_init(gfx.width, gfx.height);
    pic_unmask(1);
    pic_unmask(12);
    splash_step(3, "Input: PS/2 keyboard and IntelliMouse ready");

    /* 5. Filesystem */
    vfs_init();
    splash_step(4, "Storage: hierarchical VFS and /proc mounted");

    /* 6. Processes */
    proc_init();
    sched_init();
    seed_processes();
    splash_step(5, "Processes: PCB table and scheduler initialised");

    /* 7. Theme */
    theme_init();
    settings_init();
    splash_step(6, "Theme: Aurora palette and cached wallpaper");

    /* 8. Window manager */
    wm_init();
    splash_step(7, "Window manager: focus, drag and resize ready");

    /* 9. Desktop shell */
    desktop_init();
    splash_step(8, "Desktop shell: panel, dock and launcher ready");

    /* 10. Applications */
    splash_step(9, "Applications: registry and developer tools loaded");

    splash_finish();

    cpu_sti();
    klog(KLOG_LEVEL_OK, "Altitude OS boot complete - handing off to desktop");
    serial_write("Boot complete. Entering desktop compositor.\n");

    toast_show("Welcome to Altitude OS",
               "Press Super or click Apps to launch a program.",
               TOAST_INFO);

    desktop_run();

    /* desktop_run never returns. */
    halt_machine();
}
