/* ============================================================================
 * src/apps/proctool.c  -  Process Manager and scheduler laboratory
 * ============================================================================ */

#include "kernel.h"
#include "theme.h"
#include "gfx.h"
#include "widgets.h"
#include "mouse.h"
#include "process.h"
#include "timer.h"
#include "memory.h"
#include "apps.h"
#include "desktop.h"
#include "apps_internal.h"

typedef struct {
    int selected;
    int scroll;
    int visible_rows;
    int spawn_counter;
} process_state_t;

static int process_collect(pcb_t *table, int *out)
{
    int count = 0;
    for (int i = 0; i < PROC_TABLE_SIZE; i++) {
        if (table[i].state == STATE_UNUSED) continue;
        out[count++] = i;
    }
    return count;
}

static void proc_stat_pill(int x, int y, int w, const char *label,
                           const char *value, u32 color)
{
    gfx_round_rect(x, y, w, 28, 8, theme.panel);
    gfx_round_outline(x, y, w, 28, 8, theme.border);
    gfx_fill(x + 10, y + 9, 3, 10, color);
    gfx_text(x + 18, y + 6, label, theme.text_faint);
    gfx_text_right(x + w - 10, y + 6, value, color);
}

static void process_draw(window_t *w)
{
    process_state_t *p = (process_state_t *)w->state;
    if (!p) return;

    int x = CX(w), y = CY(w), cw = CWD(w), ch = CHT(w);
    gfx_fill(x, y, cw, ch, theme.win);

    pcb_t *table = proc_table();
    sched_stats_t st;
    sched_stats(&st);
    int order[PROC_TABLE_SIZE];
    int count = process_collect(table, order);

    if (p->selected >= count) p->selected = count - 1;
    if (p->selected < 0) p->selected = 0;

    /* Toolbar */
    gfx_fill(x, y, cw, 40, theme.panel);
    gfx_fill(x, y + 39, cw, 1, theme.border);
    gfx_fill(x + 10, y + 12, 4, 14, theme.accent);
    gfx_text(x + 22, y + 12, "Process Manager", theme.text);
    gfx_text_right(x + cw - 14, y + 12, sched_algo_name(), theme.text_faint);

    /* Algorithm row */
    int row_y = y + 50;
    gfx_text(x + 14, row_y + 7, "POLICY", theme.text_faint);

    static const char *algo_names[4] = { "FCFS", "RR", "PRIO", "SJF" };
    sched_algo_t current = sched_get_algo();
    mouse_state_t *m = mouse_get();
    int mx = m ? m->x : -1, my = m ? m->y : -1;

    int bx = x + 78;
    for (int i = 0; i < 4; i++) {
        int hot = app_hit(mx, my, bx, row_y, 56, 28);
        u32 base = ((int)current == i) ? theme.accent : (hot ? theme.panel2 : theme.bg1);
        gfx_round_rect(bx, row_y, 56, 28, 7, base);
        gfx_round_outline(bx, row_y, 56, 28, 7,
                          ((int)current == i) ? theme.accent : theme.border);
        gfx_text(bx + (56 - gfx_text_width(algo_names[i])) / 2, row_y + 6,
                 algo_names[i], ((int)current == i) ? 0x06101F : theme.text_dim);
        bx += 62;
    }

    int auto_on = app_get_auto_sched();
    int right = x + cw - 14;
    int step_x = right - 48;
    int kill_x = step_x - 52;
    int spawn_x = kill_x - 58;
    int auto_x = spawn_x - 70;

    int hot;
    hot = app_hit(mx, my, auto_x, row_y, 64, 28);
    gfx_round_rect(auto_x, row_y, 64, 28, 7, auto_on ? gfx_lerp(theme.success, 0x000000, 90) : theme.bg1);
    gfx_round_outline(auto_x, row_y, 64, 28, 7, auto_on ? theme.success : theme.border);
    gfx_text(auto_x + 10, row_y + 6, auto_on ? "AUTO" : "MANUAL",
             auto_on ? theme.success : theme.text_faint);

    hot = app_hit(mx, my, spawn_x, row_y, 52, 28);
    gfx_round_rect(spawn_x, row_y, 52, 28, 7, hot ? gfx_lerp(theme.accent, 0xFFFFFF, 26) : theme.accent);
    gfx_text(spawn_x + (52 - gfx_text_width("Spawn")) / 2, row_y + 6, "Spawn", 0x06101F);

    hot = app_hit(mx, my, kill_x, row_y, 46, 28);
    gfx_round_rect(kill_x, row_y, 46, 28, 7, hot ? gfx_lerp(theme.danger, 0xFFFFFF, 24) : theme.panel2);
    gfx_round_outline(kill_x, row_y, 46, 28, 7, theme.border);
    gfx_text(kill_x + (46 - gfx_text_width("Kill")) / 2, row_y + 6, "Kill", theme.text_dim);

    hot = app_hit(mx, my, step_x, row_y, 48, 28);
    gfx_round_rect(step_x, row_y, 48, 28, 7, hot ? theme.panel2 : theme.bg1);
    gfx_round_outline(step_x, row_y, 48, 28, 7, theme.border);
    gfx_text(step_x + (48 - gfx_text_width("Step")) / 2, row_y + 6, "Step", theme.text_dim);

    /* Stats */
    int sy = y + 88;
    int gap = 8;
    int sw = (cw - 28 - gap * 3) / 4;
    char val[48];
    ksnprintf(val, sizeof(val), "%d", sched_queue_len());
    proc_stat_pill(x + 14, sy, sw, "READY", val, theme.accent);
    ksnprintf(val, sizeof(val), "%u", st.switches);
    proc_stat_pill(x + 14 + (sw + gap), sy, sw, "SWITCHES", val, theme.accent2);
    ksnprintf(val, sizeof(val), "%u", st.completed);
    proc_stat_pill(x + 14 + (sw + gap) * 2, sy, sw, "COMPLETED", val, theme.success);
    ksnprintf(val, sizeof(val), "%d", proc_active_count());
    proc_stat_pill(x + 14 + (sw + gap) * 3, sy, sw, "ACTIVE", val, theme.warn);

    /* Process table */
    int table_y = y + 126;
    int queue_h = 88;
    int table_h = ch - (table_y - y) - queue_h - APP_STATUS_H - 16;
    if (table_h < 80) table_h = 80;

    gfx_fill(x + 12, table_y, cw - 24, 26, theme.bg0);
    gfx_text(x + 22, table_y + 5, "PID", theme.text_faint);
    gfx_text(x + 66, table_y + 5, "NAME", theme.text_faint);
    if (cw > 520) gfx_text(x + 196, table_y + 5, "STATE", theme.text_faint);
    if (cw > 620) gfx_text(x + 292, table_y + 5, "PRI", theme.text_faint);
    gfx_text_right(x + cw - 190, table_y + 5, "LEFT", theme.text_faint);
    gfx_text_right(x + cw - 120, table_y + 5, "CPU", theme.text_faint);
    gfx_text_right(x + cw - 34, table_y + 5, "BURST", theme.text_faint);

    int list_y = table_y + 26;
    int list_h = table_h - 26;
    p->visible_rows = max(1, list_h / 24);
    if (p->selected < p->scroll) p->scroll = p->selected;
    if (p->selected >= p->scroll + p->visible_rows)
        p->scroll = p->selected - p->visible_rows + 1;
    if (p->scroll > max(0, count - p->visible_rows))
        p->scroll = max(0, count - p->visible_rows);
    if (p->scroll < 0) p->scroll = 0;

    for (int row = 0; row < p->visible_rows; row++) {
        int list_index = p->scroll + row;
        if (list_index >= count) break;
        pcb_t *pcb = &table[order[list_index]];
        int ry = list_y + row * 24;
        int selected = (list_index == p->selected);
        int hot = app_hit(mx, my, x + 12, ry, cw - 24, 24);

        if (selected) gfx_fill(x + 12, ry, cw - 24, 24, theme.accent_soft);
        else if (hot) gfx_fill(x + 12, ry, cw - 24, 24, theme.panel2);

        u32 state_color = theme.text_dim;
        if (pcb->state == STATE_RUNNING) state_color = theme.success;
        else if (pcb->state == STATE_READY) state_color = theme.accent;
        else if (pcb->state == STATE_TERMINATED) state_color = theme.text_faint;

        gfx_circle_fill(x + 16, ry + 12, 3, state_color);
        gfx_textf(x + 22, ry + 4, theme.text_dim, "%d", pcb->pid);
        gfx_text(x + 66, ry + 4, pcb->name, theme.text);
        if (cw > 520) gfx_text(x + 196, ry + 4, proc_state_name(pcb->state), state_color);
        if (cw > 620) gfx_textf(x + 292, ry + 4, theme.text_dim, "%d", pcb->priority);
        gfx_textf(x + cw - 218, ry + 4, theme.text_dim, "%d", pcb->burst_left);
        gfx_textf(x + cw - 138, ry + 4, theme.text_dim, "%u", pcb->cpu_ticks);
        gfx_textf(x + cw - 78, ry + 4, theme.text_dim, "%d", pcb->burst_total);

        int bar_max = max(1, pcb->burst_total);
        int bar_w = 44;
        ui_bar(x + cw - 74, ry + 16, bar_w, 3, pcb->burst_left, bar_max, state_color, theme.bg0);
    }

    if (count == 0) {
        gfx_text_center(x + cw / 2, list_y + 36, "No processes - press Spawn",
                        theme.text_faint);
    }

    /* Ready-queue visualisation */
    int qy = y + ch - APP_STATUS_H - queue_h - 6;
    gfx_round_rect(x + 12, qy, cw - 24, queue_h, 10, theme.panel);
    gfx_round_outline(x + 12, qy, cw - 24, queue_h, 10, theme.border);
    gfx_text(x + 24, qy + 8, "READY QUEUE", theme.text_faint);
    gfx_text_right(x + cw - 24, qy + 8, sched_algo_short(), theme.accent);

    int qcount = sched_queue_len();
    if (qcount == 0) {
        gfx_text(x + 24, qy + 40, "idle - the scheduler has no runnable work",
                 theme.text_faint);
    } else {
        int chip_x = x + 24;
        int chip_y = qy + 30;
        for (int i = 0; i < qcount; i++) {
            pcb_t *q = sched_queue_at(i);
            if (!q) continue;
            int chip_w = 104;
            if (chip_x + chip_w > x + cw - 24) {
                chip_x = x + 24;
                chip_y += 28;
            }
            if (chip_y > qy + queue_h - 24) break;

            gfx_round_rect(chip_x, chip_y, chip_w, 24, 7, theme.bg1);
            gfx_round_outline(chip_x, chip_y, chip_w, 24, 7,
                              q->state == STATE_RUNNING ? theme.success : theme.border);
            gfx_textf(chip_x + 7, chip_y + 4, q->state == STATE_RUNNING ? theme.success : theme.text_dim,
                      "%d %s", q->pid, q->name);
            char brst[8];
            ksnprintf(brst, sizeof(brst), "%d", q->burst_left);
            gfx_text_right(chip_x + chip_w - 6, chip_y + 4, brst,
                           theme.text_faint);
            chip_x += chip_w + 6;
        }
    }

    char left[128];
    ksnprintf(left, sizeof(left), "%s  |  ticks %u  |  current %s",
              sched_algo_short(), st.ticks,
              st.current ? st.current->name : "idle");
    char status_right[64];
    ksnprintf(status_right, sizeof(status_right), "%d processes", count);
    app_status(w, left, status_right, theme.text_faint);
}

/* ── Actions ──────────────────────────────────────────────────────────────── */

static void process_spawn(window_t *w, process_state_t *p)
{
    pcb_t *pcb = proc_spawn("worker", 3 + (p->spawn_counter % 6),
                            8 + (p->spawn_counter * 7) % 30);
    if (pcb) {
        p->spawn_counter++;
        toast_show("Process Manager", "Spawned a new worker process", TOAST_SUCCESS);
        wm_invalidate();
    } else {
        toast_show("Process Manager", "Process table is full", TOAST_ERROR);
    }
    UNUSED(w);
}

static void process_kill(window_t *w, process_state_t *p)
{
    pcb_t *table = proc_table();
    int order[PROC_TABLE_SIZE];
    int count = process_collect(table, order);
    if (p->selected < 0 || p->selected >= count) return;
    pcb_t *pcb = &table[order[p->selected]];
    if (proc_kill(pcb->pid) == 0)
        toast_show("Process Manager", "Process terminated", TOAST_WARN);
    wm_invalidate();
}

static void process_mouse(window_t *w, int mx, int my, int button, int pressed)
{
    process_state_t *p = (process_state_t *)w->state;
    if (!p || button != MOUSE_BTN_LEFT || !pressed) return;

    int ax = mx + CX(w);
    int ay = my + CY(w);
    int x = CX(w), y = CY(w), cw = CWD(w), ch = CHT(w);

    int row_y = y + 50;
    int bx = x + 78;
    for (int i = 0; i < 4; i++) {
        if (app_hit(ax, ay, bx, row_y, 56, 28)) {
            sched_set_algo((sched_algo_t)i);
            toast_show("Scheduler", sched_algo_name(), TOAST_INFO);
            return;
        }
        bx += 62;
    }

    int right = x + cw - 14;
    int step_x = right - 48;
    int kill_x = step_x - 52;
    int spawn_x = kill_x - 58;
    int auto_x = spawn_x - 70;

    if (app_hit(ax, ay, auto_x, row_y, 64, 28)) {
        app_set_auto_sched(!app_get_auto_sched());
        return;
    }
    if (app_hit(ax, ay, spawn_x, row_y, 52, 28)) { process_spawn(w, p); return; }
    if (app_hit(ax, ay, kill_x, row_y, 46, 28)) { process_kill(w, p); return; }
    if (app_hit(ax, ay, step_x, row_y, 48, 28)) {
        sched_tick();
        return;
    }

    int table_y = y + 126;
    int list_y = table_y + 26;
    int queue_h = 88;
    int table_h = ch - (table_y - y) - queue_h - APP_STATUS_H - 16;
    if (table_h < 80) table_h = 80;

    if (my >= list_y - y && my < (list_y - y) + table_h - 26) {
        int row = (my + y - list_y) / 24;
        pcb_t *table = proc_table();
        int order[PROC_TABLE_SIZE];
        int count = process_collect(table, order);
        int index = p->scroll + row;
        if (index >= 0 && index < count) p->selected = index;
    }
}

static void process_key(window_t *w, const key_event_t *ev)
{
    process_state_t *p = (process_state_t *)w->state;
    if (!p) return;

    pcb_t *table = proc_table();
    int order[PROC_TABLE_SIZE];
    int count = process_collect(table, order);

    switch (ev->ch) {
    case 's': case 'S': process_spawn(w, p); return;
    case 'k': case 'K': process_kill(w, p); return;
    case 't': case 'T': sched_tick(); return;
    case 'a': case 'A': app_set_auto_sched(!app_get_auto_sched()); return;
    case '1': sched_set_algo(SCHED_FCFS); return;
    case '2': sched_set_algo(SCHED_RR); return;
    case '3': sched_set_algo(SCHED_PRIORITY); return;
    case '4': sched_set_algo(SCHED_SJF); return;
    default: break;
    }

    switch (ev->key) {
    case KEY_UP:
        if (p->selected > 0) p->selected--;
        break;
    case KEY_DOWN:
        if (p->selected + 1 < count) p->selected++;
        break;
    case KEY_PAGEUP:
        p->selected = max(0, p->selected - p->visible_rows);
        break;
    case KEY_PAGEDOWN:
        p->selected = min(count - 1, p->selected + p->visible_rows);
        break;
    case KEY_HOME:
        p->selected = 0;
        break;
    case KEY_END:
        p->selected = max(0, count - 1);
        break;
    default:
        break;
    }
}

static void process_wheel(window_t *w, int delta, int x, int y)
{
    process_state_t *p = (process_state_t *)w->state;
    UNUSED(x); UNUSED(y);
    if (!p) return;
    p->scroll -= delta > 0 ? 1 : -1;
    if (p->scroll < 0) p->scroll = 0;
}

static void process_close(window_t *w)
{
    if (w->state) {
        kfree(w->state);
        w->state = NULL;
    }
}

void app_process_open(void)
{
    window_t *w = wm_find(APP_PROCESS);
    if (w) {
        wm_focus(w);
        return;
    }

    process_state_t *p = (process_state_t *)kzalloc(sizeof(process_state_t));
    if (!p) return;
    p->selected = 0;
    p->scroll = 0;
    p->visible_rows = 10;

    int ww = min(940, max(620, gfx.width - 160));
    int wh = min(620, max(400, gfx.height - 180));
    int wx = max(30, (gfx.width - ww) / 2 - 10);
    int wy = max(50, (gfx.height - wh) / 2 - 10);

    window_t *win = wm_create(APP_PROCESS, "Process Manager", wx, wy, ww, wh,
                              600, 380, process_draw, process_key, process_mouse,
                              process_wheel, process_close, p);
    if (!win) kfree(p);
}
