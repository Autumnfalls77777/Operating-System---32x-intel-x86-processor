/* ============================================================================
 * src/kernel/syscall.c  -  INT 0x80 system call implementation
 * ============================================================================ */

#include "kernel.h"
#include "syscall.h"
#include "interrupts.h"
#include "timer.h"
#include "memory.h"
#include "vfs.h"
#include "process.h"
#include "keyboard.h"
#include "serial.h"

static u32 call_count;

static const syscall_info_t table[SYS_COUNT] = {
    [SYS_NONE]       = { "none",       0, "reserved" },
    [SYS_WRITE]      = { "write",      4, "write buffer to fd (1=kernel log, 2=serial)" },
    [SYS_READ]       = { "read",       2, "read queued keyboard characters" },
    [SYS_GETPID]     = { "getpid",     0, "return the calling pid (0 = kernel)" },
    [SYS_UPTIME]     = { "uptime",     0, "milliseconds since boot" },
    [SYS_TICKS]      = { "ticks",      0, "PIT timer ticks" },
    [SYS_ALLOC]      = { "alloc",      1, "allocate kernel heap memory" },
    [SYS_FREE]       = { "free",       1, "free kernel heap memory" },
    [SYS_OPEN]       = { "open",       2, "resolve path -> node id" },
    [SYS_READFILE]   = { "readfile",   4, "read a file into a buffer" },
    [SYS_WRITEFILE]  = { "writefile",  5, "write/append a file" },
    [SYS_UNLINK]     = { "unlink",     2, "remove a file or empty directory" },
    [SYS_MKDIR]      = { "mkdir",      2, "create a directory" },
    [SYS_SPAWN]      = { "spawn",      3, "create a process (name, priority, burst)" },
    [SYS_KILL]       = { "kill",       1, "terminate a process" },
    [SYS_SCHED_SET]  = { "sched_set",  1, "change scheduling algorithm" },
    [SYS_SCHED_GET]  = { "sched_get",  0, "return current scheduler" },
    [SYS_SCHED_TICK] = { "sched_tick", 0, "advance the scheduler one tick" },
    [SYS_MEMINFO]    = { "meminfo",    1, "copy mem_info_t into a buffer" },
    [SYS_UNAME]      = { "uname",      2, "write the kernel release string" },
    [SYS_GETCHAR]    = { "getchar",    0, "non-blocking keyboard read" },
    [SYS_BEEP]       = { "beep",       2, "PC speaker tone (hz, ms)" },
    [SYS_REBOOT]     = { "reboot",     0, "reset the machine" },
    [SYS_HALT]       = { "halt",       0, "halt the CPU" },
};

u32 syscall_count(void) { return call_count; }

const syscall_info_t *syscall_info(int num)
{
    if (num < 0 || num >= SYS_COUNT || !table[num].name) return NULL;
    return &table[num];
}

static void beep(u32 hz, u32 ms)
{
    if (hz == 0 || ms == 0) return;
    u32 divisor = 1193180u / hz;
    port_outb(0x43, 0xB6);
    port_outb(0x42, (u8)(divisor & 0xFF));
    port_outb(0x42, (u8)((divisor >> 8) & 0xFF));
    u8 tmp = port_inb(0x61);
    port_outb(0x61, (u8)(tmp | 0x03));
    timer_sleep_ms(ms);
    port_outb(0x61, (u8)(tmp & 0xFC));
}

void syscall_handler(regs_t *r)
{
    call_count++;
    u32 num = r->eax;
    u32 a = r->ebx;
    u32 b = r->ecx;
    u32 c = r->edx;
    u32 d = r->esi;
    u32 e = r->edi;
    u32 ret = (u32)-1;

    switch (num) {
    case SYS_WRITE: {
        const char *buf = (const char *)b;
        u32 len = c;
        if (a == 2) {
            serial_write_len(buf, (int)len);
        } else {
            char line[128];
            u32 n = min(len, (u32)sizeof(line) - 1);
            memcpy(line, buf, n);
            line[n] = '\0';
            klog(KLOG_LEVEL_INFO, "sys_write: %s", line);
        }
        ret = len;
        break;
    }
    case SYS_READ: {
        char *buf = (char *)a;
        u32 cap = b;
        u32 n = 0;
        key_event_t ev;
        while (n < cap && keyboard_poll(&ev)) {
            if (ev.ch) buf[n++] = (char)ev.ch;
        }
        ret = n;
        break;
    }
    case SYS_GETPID:
        ret = 0;
        break;
    case SYS_UPTIME:
        ret = timer_ms();
        break;
    case SYS_TICKS:
        ret = timer_ticks();
        break;
    case SYS_ALLOC:
        ret = (u32)kmalloc(a);
        break;
    case SYS_FREE:
        kfree((void *)a);
        ret = 0;
        break;
    case SYS_OPEN:
        ret = (u32)vfs_lookup((const char *)a, (const char *)b);
        break;
    case SYS_READFILE:
        ret = (u32)vfs_read_file((const char *)a, (const char *)b,
                                 (char *)c, (int)d);
        break;
    case SYS_WRITEFILE:
        ret = (u32)vfs_write_file((const char *)a, (const char *)b,
                                  (const char *)c, d, (int)e);
        break;
    case SYS_UNLINK:
        ret = (u32)vfs_unlink((const char *)a, (const char *)b);
        break;
    case SYS_MKDIR:
        ret = (u32)vfs_mkdir((const char *)a, (const char *)b);
        break;
    case SYS_SPAWN: {
        pcb_t *p = proc_spawn((const char *)a, (int)b, (int)c);
        ret = p ? (u32)p->pid : 0;
        break;
    }
    case SYS_KILL:
        ret = (u32)proc_kill((int)a);
        break;
    case SYS_SCHED_SET: {
        sched_algo_t old = sched_get_algo();
        sched_set_algo((sched_algo_t)a);
        ret = (u32)old;
        break;
    }
    case SYS_SCHED_GET:
        ret = (u32)sched_get_algo();
        break;
    case SYS_SCHED_TICK: {
        pcb_t *p = sched_tick();
        ret = p ? (u32)p->pid : 0;
        break;
    }
    case SYS_MEMINFO: {
        mem_info_t info;
        mem_info(&info);
        if (a && b >= sizeof(info)) {
            memcpy((void *)a, &info, sizeof(info));
            ret = sizeof(info);
        }
        break;
    }
    case SYS_UNAME: {
        const char *name = "Altitude OS " NOVA_VERSION " i386 (" NOVA_CODENAME ")";
        int len = kstrlen(name);
        if (a && b > 0) {
            int n = min(len, (int)b - 1);
            memcpy((void *)a, name, (size_t)n);
            ((char *)a)[n] = '\0';
            ret = (u32)n;
        }
        break;
    }
    case SYS_GETCHAR: {
        key_event_t ev;
        ret = keyboard_poll(&ev) && ev.ch ? (u32)ev.ch : 0;
        break;
    }
    case SYS_BEEP:
        beep(a, b);
        ret = 0;
        break;
    case SYS_REBOOT:
        reboot_machine();
        break;
    case SYS_HALT:
        halt_machine();
        break;
    default:
        ret = (u32)-1;
        break;
    }

    r->eax = ret;
}

u32 sys_call(u32 num, u32 a, u32 b, u32 c, u32 d, u32 e)
{
    u32 ret;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                      : "memory", "cc");
    return ret;
}
