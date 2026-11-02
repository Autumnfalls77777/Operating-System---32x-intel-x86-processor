/* ============================================================================
 * include/process.h  -  Process control blocks and CPU scheduling
 * ============================================================================ */

#ifndef NOVA_PROCESS_H
#define NOVA_PROCESS_H

#include "types.h"

#define PROC_TABLE_SIZE  24
#define PROC_NAME_LEN    24

typedef enum {
    STATE_UNUSED     = 0,
    STATE_READY      = 1,
    STATE_RUNNING    = 2,
    STATE_WAITING    = 3,
    STATE_TERMINATED = 4,
} proc_state_t;

typedef enum {
    SCHED_FCFS     = 0,
    SCHED_RR       = 1,
    SCHED_PRIORITY = 2,
    SCHED_SJF      = 3,
} sched_algo_t;

typedef struct {
    int          pid;
    char         name[PROC_NAME_LEN];
    proc_state_t state;
    int          priority;        /* 1 (highest) .. 10 (lowest) */
    int          burst_total;
    int          burst_left;
    int          arrival_tick;
    int          rr_slice_used;
    u32          cpu_ticks;
    u32          created_tick;
    u32          finished_tick;
} pcb_t;

typedef struct {
    u32 ticks;
    u32 switches;
    u32 completed;
    int queue_len;
    pcb_t *current;
} sched_stats_t;

/* ── Process API ─────────────────────────────────────────────────────────── */

void   proc_init(void);
pcb_t *proc_spawn(const char *name, int prio, int burst);
int    proc_kill(int pid);
pcb_t *proc_find(int pid);
pcb_t *proc_table(void);
int    proc_active_count(void);
int    proc_total_spawned(void);
int    proc_completed_count(void);
const char *proc_state_name(proc_state_t s);

/* ── Scheduler API ───────────────────────────────────────────────────────── */

void        sched_init(void);
void        sched_set_algo(sched_algo_t algo);
sched_algo_t sched_get_algo(void);
const char *sched_algo_name(void);
const char *sched_algo_short(void);
void        sched_add(pcb_t *p);
void        sched_remove(pcb_t *p);
pcb_t      *sched_tick(void);
pcb_t      *sched_current(void);
int         sched_queue_len(void);
int         sched_run_until_idle(int max_ticks);
void        sched_stats(sched_stats_t *out);
pcb_t      *sched_queue_at(int index);

#endif /* NOVA_PROCESS_H */
