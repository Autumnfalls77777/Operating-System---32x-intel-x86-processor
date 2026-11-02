/* ============================================================================
 * src/drivers/timer.c  -  Programmable Interval Timer (100 Hz)
 * ============================================================================ */

#include "kernel.h"
#include "timer.h"
#include "interrupts.h"

#define PIT_FREQUENCY 1193182u

static volatile u32 ticks;

void timer_init(void)
{
    u32 divisor = PIT_FREQUENCY / TIMER_HZ;
    ticks = 0;

    port_outb(0x43, 0x36);                  /* channel 0, lobyte/hibyte, mode 3 */
    port_outb(0x40, (u8)(divisor & 0xFF));
    port_outb(0x40, (u8)((divisor >> 8) & 0xFF));
}

void timer_irq(void)
{
    ticks++;
}

u32 timer_ticks(void) { return ticks; }
u32 timer_ms(void) { return ticks * (1000u / TIMER_HZ); }
u32 timer_seconds(void) { return ticks / TIMER_HZ; }
u32 timer_irq_count(void) { return irq_count(0); }

void timer_sleep_ticks(u32 count)
{
    u32 target = ticks + count;
    while ((int)(ticks - target) < 0) cpu_hlt();
}

void timer_sleep_ms(u32 ms)
{
    u32 count = (ms + (1000u / TIMER_HZ) - 1) / (1000u / TIMER_HZ);
    timer_sleep_ticks(count);
}
