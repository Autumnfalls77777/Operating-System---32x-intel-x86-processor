/* ============================================================================
 * src/kernel/klog.c  -  Kernel log ring buffer, mirrored to COM1
 * ============================================================================ */

#include "kernel.h"
#include "serial.h"
#include "timer.h"

#define KLOG_MAX 128
#define KLOG_LEN 160

static char log_lines[KLOG_MAX][KLOG_LEN];
static int  log_levels[KLOG_MAX];
static int  log_count;
static int  log_next;

void klog_init(void)
{
    log_count = 0;
    log_next = 0;
    for (int i = 0; i < KLOG_MAX; i++) {
        log_lines[i][0] = '\0';
        log_levels[i] = KLOG_LEVEL_INFO;
    }
}

void klog(int level, const char *fmt, ...)
{
    char text[KLOG_LEN];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kvsnprintf(text, sizeof(text), fmt, ap);
    __builtin_va_end(ap);

    u32 ms = timer_ticks() * (1000u / TIMER_HZ);
    char line[KLOG_LEN];
    ksnprintf(line, sizeof(line), "[%5u.%02u] %s",
              ms / 1000u, (ms / 10u) % 100u, text);

    int slot = log_next;
    kstrncpy(log_lines[slot], line, KLOG_LEN);
    log_levels[slot] = level;
    log_next = (log_next + 1) % KLOG_MAX;
    if (log_count < KLOG_MAX) log_count++;

    static const char *prefix[] = { "  ", "OK", "!!", "XX", "BOOT" };
    int p = (level >= 0 && level <= KLOG_LEVEL_BOOT) ? level : KLOG_LEVEL_INFO;
    serial_printf("[%s] %s\n", prefix[p], text);
}

int klog_count(void)
{
    return log_count;
}

const char *klog_line(int index, int *level)
{
    if (index < 0 || index >= log_count) return NULL;
    int start = (log_next - log_count + KLOG_MAX) % KLOG_MAX;
    int slot = (start + index) % KLOG_MAX;
    if (level) *level = log_levels[slot];
    return log_lines[slot];
}
