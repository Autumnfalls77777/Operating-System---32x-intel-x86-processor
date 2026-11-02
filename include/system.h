/* ============================================================================
 * include/system.h  -  Global system information
 * ============================================================================ */

#ifndef NOVA_SYSTEM_H
#define NOVA_SYSTEM_H

#include "types.h"
#include "bootinfo.h"

extern boot_info_t *g_boot_info;

u32 system_total_ram_kb(void);
u32 system_usable_ram_kb(void);
const char *system_arch(void);

#endif /* NOVA_SYSTEM_H */
