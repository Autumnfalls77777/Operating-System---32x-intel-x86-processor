/* ============================================================================
 * include/syscall.h  -  Software-interrupt system call interface
 *
 * The kernel is single-address-space (no user mode yet), so system calls are
 * available to kernel modules and to the developer shell through INT 0x80.
 * ============================================================================ */

#ifndef NOVA_SYSCALL_H
#define NOVA_SYSCALL_H

#include "types.h"

enum {
    SYS_NONE = 0,
    SYS_WRITE,          /* 1  ebx=fd (1=console,2=serial), ecx=buf, edx=len */
    SYS_READ,           /* 2  ebx=buf, ecx=len -> bytes read                  */
    SYS_GETPID,         /* 3  -> pid (0 = kernel)                            */
    SYS_UPTIME,         /* 4  -> milliseconds                                 */
    SYS_TICKS,          /* 5  -> timer ticks                                  */
    SYS_ALLOC,          /* 6  ebx=bytes -> pointer                            */
    SYS_FREE,           /* 7  ebx=pointer                                     */
    SYS_OPEN,           /* 8  ebx=path, ecx=cwd -> node id                    */
    SYS_READFILE,       /* 9  ebx=path, ecx=cwd, edx=buf, esi=cap -> length   */
    SYS_WRITEFILE,      /* 10 ebx=path, ecx=cwd, edx=buf, esi=len, edi=append */
    SYS_UNLINK,         /* 11 ebx=path, ecx=cwd -> 0/-1                       */
    SYS_MKDIR,          /* 12 ebx=path, ecx=cwd -> 0/-1                       */
    SYS_SPAWN,          /* 13 ebx=name, ecx=prio, edx=burst -> pid            */
    SYS_KILL,           /* 14 ebx=pid -> 0/-1                                 */
    SYS_SCHED_SET,      /* 15 ebx=algorithm -> previous algorithm             */
    SYS_SCHED_GET,      /* 16 -> algorithm                                    */
    SYS_SCHED_TICK,     /* 17 -> pid that ran, 0 if idle                      */
    SYS_MEMINFO,        /* 18 ebx=mem_info_t* -> bytes filled                 */
    SYS_UNAME,          /* 19 ebx=buf, ecx=cap -> length                      */
    SYS_GETCHAR,        /* 20 non-blocking -> ASCII or 0                       */
    SYS_BEEP,           /* 21 ebx=hz, ecx=ms (PC speaker)                     */
    SYS_REBOOT,         /* 22 does not return                                 */
    SYS_HALT,           /* 23 does not return                                 */
    SYS_COUNT
};

typedef struct {
    const char *name;
    int         args;
    const char *description;
} syscall_info_t;

struct regs;

u32  sys_call(u32 num, u32 a, u32 b, u32 c, u32 d, u32 e);
void syscall_handler(struct regs *r);
u32  syscall_count(void);
const syscall_info_t *syscall_info(int num);

#endif /* NOVA_SYSCALL_H */
