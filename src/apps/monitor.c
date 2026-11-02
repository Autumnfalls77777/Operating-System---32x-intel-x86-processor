/* ============================================================================
 * src/apps/monitor.c  -  System Monitor: live heap, process and IRQ graphs
 * ============================================================================ */

#include "kernel.h"
#include "theme.h"
#include "gfx.h"
#include "widgets.h"
#include "mouse.h"
#include "timer.h"
#include "memory.h"
#include "process.h"
#include "interrupts.h"
#include "syscall.h"
#include "system.h"
#include "vfs.h"
#include "cpu.h"
#include "apps.h"
#include "apps_internal.h"

#define MON_HIST 64

typedef struct {
    u32 heap[MON_HIST];      /* KiB used            */
    u32 procs[MON_HIST];     /* ready + running     */
    u32 irqs[MON_HIST];      /* interrupts/second   */
    int count;
    int pos;
    u32 last_irq_total;
    u32 last_second;
} monitor_state_t;

static monitor_state_t mon_history;
static int mon_initialised;

static void monitor_push(monitor_state_t *m, u32 heap, u32 procs, u32 irqs)
{
    m->heap[m->pos] = heap;
    m->procs[m->pos] = procs;
    m->irqs[m->pos] = irqs;
    m->pos = (m->pos + 1) % MON_HIST;
    if (m->count < MON_HIST) m->count++;
}

void monitor_second(void)
{
    if (!mon_initialised) {
        memset(&mon_history, 0, sizeof(mon_history));
        mon_history.last_irq_total = interrupt_total_count();
        mon_initialised = 1;
    }

    u32 irq_total = interrupt_total_count();
    u32 irq_delta = irq_total - mon_history.last_irq_total;
    mon_history.last_irq_total = irq_total;

    mem_info_t mi;
    mem_info(&mi);
    monitor_push(&mon_history, mi.used / 1024,
                 (u32)(proc_active_count() + sched_queue_len()),
                 irq_delta);
    mon_history.last_second = timer_seconds();
}

static monitor_state_t *monitor_state_for(window_t *w)
{
    UNUSED(w);
    return &mon_history;
}

/* ── Card rendering ───────────────────────────────────────────────────────── */

static void draw_card(int x, int y, int w, int h, const char *title,
                      const char *value, const char *sub, u32 color,
                      int bar_value, int bar_max)
{
    gfx_round_rect(x, y, w, h, 12, theme.panel);
    gfx_round_outline(x, y, w, h, 12, theme.border);
    gfx_fill(x + 14, y + 13, 4, 4, color);
    gfx_text(x + 24, y + 8, title, theme.text_faint);
    gfx_text_scale(x + 14, y + 30, value, color, 2);
    if (sub) gfx_text(x + 14, y + 66, sub, theme.text_dim);
    if (bar_max > 0)
        ui_bar(x + 14, y + h - 18, w - 28, 6, bar_value, bar_max, color, theme.bg0);
}

static u32 history_at(monitor_state_t *m, u32 *array, int index)
{
    if (m->count <= 0) return 0;
    int start = (m->pos - m->count + MON_HIST) % MON_HIST;
    return array[(start + index) % MON_HIST];
}

static void draw_graph(int x, int y, int w, int h, const char *title,
                       monitor_state_t *m, u32 *samples, u32 color,
                       const char *unit)
{
    gfx_round_rect(x, y, w, h, 12, theme.panel);
    gfx_round_outline(x, y, w, h, 12, theme.border);
    gfx_text(x + 14, y + 10, title, theme.text_dim);

    int px = x + 14;
    int py = y + 34;
    int pw = w - 28;
    int ph = h - 58;
    if (pw < 20 || ph < 20) return;

    u32 max_value = 1;
    for (int i = 0; i < m->count; i++) {
        u32 v = history_at(m, samples, i);
        if (v > max_value) max_value = v;
    }
    max_value = ((max_value + 9) / 10) * 10;

    /* Grid */
    for (int i = 0; i <= 4; i++) {
        int gy = py + (ph * i) / 4;
        gfx_fill(px, gy, pw, 1, theme.bg1);
    }

    char max_label[32];
    ksnprintf(max_label, sizeof(max_label), "%u%s", max_value, unit);
    gfx_text_right(x + w - 10, y + 10, max_label, theme.text_faint);

    int count = max(1, m->count);
    int bw = pw / count;
    if (bw < 1) bw = 1;

    for (int i = 0; i < m->count; i++) {
        u32 v = history_at(m, samples, i);
        int bh = (int)((v * (u32)ph) / max_value);
        int bx = px + i * bw;
        int by = py + ph - bh;
        if (bh > 0) gfx_fill_blend(bx, by, max(1, bw - 1), bh, color, 54);
        gfx_fill(bx, by, max(1, bw - 1), 1, color);
    }

    gfx_fill(px, py + ph, pw, 1, theme.border);
    gfx_fill(px, py, 1, ph, theme.border);

    char now[48];
    ksnprintf(now, sizeof(now), "now: %u%s", history_at(m, samples, m->count - 1), unit);
    gfx_text(x + 14, y + h - 18, now, color);
}

/* ── Main window ──────────────────────────────────────────────────────────── */

static void monitor_draw(window_t *w)
{
    monitor_state_t *m = monitor_state_for(w);
    int x = CX(w), y = CY(w), cw = CWD(w), ch = CHT(w);
    gfx_fill(x, y, cw, ch, theme.win);

    mem_info_t mi;
    mem_info(&mi);
    sched_stats_t st;
    sched_stats(&st);
    cpu_info_t cpu;
    cpuid_info(&cpu);

    int pad = 12;
    int card_y = y + 46;
    int card_h = 96;
    int gap = 10;
    int card_w = (cw - pad * 2 - gap * 3) / 4;
    if (card_w < 120) card_w = 120;

    char value[48], sub[96];

    u32 total_mb = mi.total / (1024u * 1024u);
    ksnprintf(value, sizeof(value), "%u.%u", mi.used / (1024u * 1024u),
              (mi.used / 1024u) % 1024u * 10u / 1024u);
    ksnprintf(sub, sizeof(sub), "of %u MiB kernel heap", total_mb);
    draw_card(x + pad, card_y, card_w, card_h, "MEMORY", value, sub,
              theme.accent, (int)(mi.used / 1024u), (int)(mi.total / 1024u));

    ksnprintf(value, sizeof(value), "%d", proc_active_count());
    ksnprintf(sub, sizeof(sub), "queue %d  |  done %d", sched_queue_len(), proc_completed_count());
    draw_card(x + pad + card_w + gap, card_y, card_w, card_h, "PROCESSES",
              value, sub, theme.success, proc_active_count(), 24);

    ksnprintf(value, sizeof(value), "%u", interrupt_total_count());
    ksnprintf(sub, sizeof(sub), "timer %u  |  syscalls %u", timer_ticks(), syscall_count());
    draw_card(x + pad + (card_w + gap) * 2, card_y, card_w, card_h,
              "INTERRUPTS", value, sub, theme.warn,
              (int)irq_count(0), (int)max(1u, interrupt_total_count()));

    u32 secs = timer_seconds();
    ksnprintf(value, sizeof(value), "%u:%02u", secs / 60u, secs % 60u);
    ksnprintf(sub, sizeof(sub), "up  |  vfs %d nodes", vfs_used_nodes());
    draw_card(x + pad + (card_w + gap) * 3, card_y, card_w, card_h,
              "UPTIME", value, sub, theme.accent2,
              (int)(secs % 60u), 60);

    int gy = card_y + card_h + 12;
    int gh = ch - (gy - y) - APP_STATUS_H - 12;
    if (gh < 120) gh = 120;
    int gw = (cw - pad * 2 - gap) / 2;

    draw_graph(x + pad, gy, gw, gh, "KERNEL HEAP  (KiB)", m, m->heap,
               theme.accent, "K");
    draw_graph(x + pad + gw + gap, gy, gw, gh, "INTERRUPTS / SECOND", m, m->irqs,
               theme.warn, "");

    char left[160];
    ksnprintf(left, sizeof(left), "%s  |  switches %u  |  cpu %s",
              sched_algo_short(), st.switches, cpu.vendor);
    char right[128];
    ksnprintf(right, sizeof(right), "%u KiB heap  |  ready %d", mi.used / 1024,
              sched_queue_len());
    app_status(w, left, right, theme.text_faint);
}

static void monitor_close(window_t *w)
{
    /* History is static; nothing to free. */
    w->state = NULL;
}

void app_monitor_open(void)
{
    window_t *w = wm_find(APP_MONITOR);
    if (w) {
        wm_focus(w);
        return;
    }

    int ww = min(980, max(640, gfx.width - 140));
    int wh = min(640, max(400, gfx.height - 160));
    int wx = max(20, (gfx.width - ww) / 2 - 6);
    int wy = max(46, (gfx.height - wh) / 2 - 12);

    w = wm_create(APP_MONITOR, "System Monitor", wx, wy, ww, wh,
                  620, 400, monitor_draw, NULL, NULL, NULL, monitor_close, NULL);
    if (w) wm_focus(w);
}
