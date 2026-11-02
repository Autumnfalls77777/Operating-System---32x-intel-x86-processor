/* ============================================================================
 * include/splash.h  -  Graphical boot splash
 * ============================================================================ */

#ifndef NOVA_SPLASH_H
#define NOVA_SPLASH_H

#include "types.h"
#include "bootinfo.h"

void splash_begin(boot_info_t *bi);
void splash_stage(int index, int total, const char *text, int ok);
void splash_finish(void);

#endif /* NOVA_SPLASH_H */
