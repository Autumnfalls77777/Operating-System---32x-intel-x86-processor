/* ============================================================================
 * src/kernel/system.c  -  Global system information helpers
 * ============================================================================ */

#include "kernel.h"
#include "system.h"

boot_info_t *g_boot_info = NULL;

u32 system_usable_ram_kb(void)
{
    if (!g_boot_info) return 0;
    u32 bytes = 0;
    for (u32 i = 0; i < g_boot_info->e820_count && i < 16; i++) {
        if (g_boot_info->e820[i].type == 1)
            bytes += g_boot_info->e820[i].length_lo;
    }
    return bytes / 1024u;
}

u32 system_total_ram_kb(void)
{
    u32 usable = system_usable_ram_kb();
    if (usable) return usable;
    if (g_boot_info)
        return g_boot_info->mem_lower_kb + g_boot_info->mem_upper_kb;
    return 0;
}

const char *system_arch(void)
{
    return "i386 (32-bit protected mode)";
}
