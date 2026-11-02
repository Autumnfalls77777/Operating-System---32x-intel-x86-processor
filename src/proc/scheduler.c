/* ============================================================================
 * src/proc/scheduler.c  -  FCFS, Round-Robin, Priority and SJF scheduling
 * ============================================================================ */

#include "kernel.h"
#include "process.h"
#include "timer.h"

#define SCHED_QUEUE_SIZE 32
#define RR_QUANTUM 3

static pcb_t *ready_queue[SCHED_QUEUE_SIZE];
static int queue_count;
static pcb_t *current_process;
static sched_algo_t current_algo = SCHED_RR;
static sched_stats_t stats;

static const char *algo_long[] = {
    "FCFS (First Come First Served)",
    "Round Robin (quantum = 3 ticks)",
    "Priority (lowest number first)",
    "SJF (shortest remaining burst first)"
};

static const char *algo_short[] = { "FCFS", "RR", "PRIO", "SJF" };

static void remove_at(int index)
{
    if (index < 0 || index >= queue_count) return;
    for (int i = index; i + 1 < queue_count; i++)
        ready_queue[i] = ready_queue[i + 1];
    queue_count--;
}

static int queue_index(pcb_t *p)
{
    for (int i = 0; i < queue_count; i++)
        if (ready_queue[i] == p) return i;
    return -1;
}

static void purge_queue(void)
{
    for (int i = 0; i < queue_count; i++) {
        pcb_t *p = ready_queue[i];
        if (p->state == STATE_TERMINATED || p->burst_left <= 0) {
            if (p->state != STATE_TERMINATED) {
                p->state = STATE_TERMINATED;
                p->finished_tick = timer_ticks();
                stats.completed++;
                klog(KLOG_LEVEL_INFO, "process complete pid=%d name=%s",
                     p->pid, p->name);
            }
            if (current_process == p) current_process = NULL;
            remove_at(i--);
        }
    }
}

static pcb_t *select_next(void)
{
    if (queue_count <= 0) return NULL;
    int selected = 0;

    if (current_algo == SCHED_PRIORITY) {
        for (int i = 1; i < queue_count; i++)
            if (ready_queue[i]->priority < ready_queue[selected]->priority)
                selected = i;
    } else if (current_algo == SCHED_SJF) {
        for (int i = 1; i < queue_count; i++)
            if (ready_queue[i]->burst_left < ready_queue[selected]->burst_left)
                selected = i;
    }

    return ready_queue[selected];
}

void sched_init(void)
{
    queue_count = 0;
    current_process = NULL;
    current_algo = SCHED_RR;
    memset(&stats, 0, sizeof(stats));
}

void sched_set_algo(sched_algo_t algo)
{
    if (algo < SCHED_FCFS || algo > SCHED_SJF) algo = SCHED_RR;
    current_algo = algo;
    for (int i = 0; i < queue_count; i++)
        ready_queue[i]->rr_slice_used = 0;
    klog(KLOG_LEVEL_INFO, "scheduler policy -> %s", algo_long[algo]);
}

sched_algo_t sched_get_algo(void) { return current_algo; }
const char *sched_algo_name(void) { return algo_long[current_algo]; }
const char *sched_algo_short(void) { return algo_short[current_algo]; }

void sched_add(pcb_t *p)
{
    if (!p || queue_count >= SCHED_QUEUE_SIZE) return;
    if (queue_index(p) >= 0) return;
    ready_queue[queue_count++] = p;
    if (p->state != STATE_RUNNING) p->state = STATE_READY;
}

void sched_remove(pcb_t *p)
{
    int idx = queue_index(p);
    if (idx >= 0) remove_at(idx);
    if (current_process == p) current_process = NULL;
}

pcb_t *sched_tick(void)
{
    purge_queue();
    stats.queue_len = queue_count;

    if (queue_count == 0) {
        current_process = NULL;
        return NULL;
    }

    pcb_t *next = select_next();
    if (!next) {
        current_process = NULL;
        return NULL;
    }

    if (current_process != next) {
        if (current_process && current_process->state == STATE_RUNNING)
            current_process->state = STATE_READY;
        current_process = next;
        current_process->state = STATE_RUNNING;
        stats.switches++;
    }

    next->burst_left--;
    next->cpu_ticks++;
    stats.ticks++;

    if (next->burst_left <= 0) {
        next->state = STATE_TERMINATED;
        next->finished_tick = timer_ticks();
        stats.completed++;
        klog(KLOG_LEVEL_INFO, "process complete pid=%d name=%s",
             next->pid, next->name);
        sched_remove(next);
        current_process = NULL;
    } else if (current_algo == SCHED_RR) {
        next->rr_slice_used++;
        if (next->rr_slice_used >= RR_QUANTUM) {
            next->rr_slice_used = 0;
            next->state = STATE_READY;
            int idx = queue_index(next);
            if (idx >= 0) {
                remove_at(idx);
                ready_queue[queue_count++] = next;
            }
            current_process = NULL;
        }
    }

    stats.queue_len = queue_count;
    stats.current = current_process;
    return next;
}

pcb_t *sched_current(void) { return current_process; }
int sched_queue_len(void) { return queue_count; }

pcb_t *sched_queue_at(int index)
{
    if (index < 0 || index >= queue_count) return NULL;
    return ready_queue[index];
}

int sched_run_until_idle(int max_ticks)
{
    int n = 0;
    while (queue_count > 0 && n < max_ticks) {
        sched_tick();
        n++;
    }
    return n;
}

void sched_stats(sched_stats_t *out)
{
    if (!out) return;
    *out = stats;
    out->queue_len = queue_count;
    out->current = current_process;
}
