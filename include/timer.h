/* ============================================================================
 * include/timer.h  -  PIT timer and time helpers
 * ============================================================================ */

#ifndef NOVA_TIMER_H
#define NOVA_TIMER_H

#include "types.h"

void timer_init(void);
void timer_irq(void);

u32  timer_ticks(void);
u32  timer_ms(void);
u32  timer_seconds(void);
u32  timer_irq_count(void);

void timer_sleep_ms(u32 ms);
void timer_sleep_ticks(u32 ticks);

#endif /* NOVA_TIMER_H */
