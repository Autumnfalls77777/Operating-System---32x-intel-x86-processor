/* ============================================================================
 * src/proc/process.c  -  Process control blocks
 * ============================================================================ */

#include "kernel.h"
#include "process.h"
#include "timer.h"

static pcb_t process_table[PROC_TABLE_SIZE];
static int next_pid = 1;
static int total_spawned;

void proc_init(void)
{
    for (int i = 0; i < PROC_TABLE_SIZE; i++) {
        process_table[i].pid = 0;
        process_table[i].state = STATE_UNUSED;
        process_table[i].name[0] = '\0';
        process_table[i].burst_left = 0;
        process_table[i].burst_total = 0;
        process_table[i].cpu_ticks = 0;
    }
    next_pid = 1;
    total_spawned = 0;
}

pcb_t *proc_spawn(const char *name, int prio, int burst)
{
    for (int i = 0; i < PROC_TABLE_SIZE; i++) {
        pcb_t *p = &process_table[i];
        if (p->state != STATE_UNUSED && p->state != STATE_TERMINATED) continue;

        memset(p, 0, sizeof(pcb_t));
        p->pid = next_pid++;
        kstrncpy(p->name, name, PROC_NAME_LEN);
        p->state = STATE_READY;
        p->priority = clamp(prio, 1, 10);
        p->burst_total = burst < 1 ? 1 : burst;
        p->burst_left = p->burst_total;
        p->arrival_tick = (int)timer_ticks();
        p->created_tick = timer_ticks();
        p->rr_slice_used = 0;

        sched_add(p);
        total_spawned++;
        klog(KLOG_LEVEL_INFO, "spawn pid=%d name=%s prio=%d burst=%d",
             p->pid, p->name, p->priority, p->burst_total);
        return p;
    }

    klog(KLOG_LEVEL_WARN, "process table full");
    return NULL;
}

int proc_kill(int pid)
{
    pcb_t *p = proc_find(pid);
    if (!p) return -1;

    p->state = STATE_TERMINATED;
    p->finished_tick = timer_ticks();
    sched_remove(p);
    klog(KLOG_LEVEL_INFO, "kill pid=%d name=%s", p->pid, p->name);
    return 0;
}

pcb_t *proc_find(int pid)
{
    for (int i = 0; i < PROC_TABLE_SIZE; i++) {
        if (process_table[i].pid == pid &&
            process_table[i].state != STATE_UNUSED &&
            process_table[i].state != STATE_TERMINATED)
            return &process_table[i];
    }
    return NULL;
}

pcb_t *proc_table(void) { return process_table; }

int proc_active_count(void)
{
    int n = 0;
    for (int i = 0; i < PROC_TABLE_SIZE; i++) {
        proc_state_t s = process_table[i].state;
        if (s == STATE_READY || s == STATE_RUNNING || s == STATE_WAITING) n++;
    }
    return n;
}

int proc_total_spawned(void) { return total_spawned; }
int proc_completed_count(void);
int proc_completed_count(void)
{
    sched_stats_t st;
    sched_stats(&st);
    return (int)st.completed;
}

const char *proc_state_name(proc_state_t s)
{
    switch (s) {
    case STATE_READY:      return "READY";
    case STATE_RUNNING:    return "RUNNING";
    case STATE_WAITING:    return "WAITING";
    case STATE_TERMINATED: return "DONE";
    default:               return "UNUSED";
    }
}
