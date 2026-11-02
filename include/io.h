/* ============================================================================
 * include/io.h  -  x86 I/O ports, control registers and CPU primitives
 * ============================================================================ */

#ifndef NOVA_IO_H
#define NOVA_IO_H

#include "types.h"

static inline u8 port_inb(u16 port)
{
    u8 v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void port_outb(u16 port, u8 v)
{
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline u16 port_inw(u16 port)
{
    u16 v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void port_outw(u16 port, u16 v)
{
    __asm__ volatile ("outw %0, %1" : : "a"(v), "Nd"(port));
}

static inline u32 port_inl(u16 port)
{
    u32 v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void port_outl(u16 port, u32 v)
{
    __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"(port));
}

static inline void io_wait(void)
{
    port_outb(0x80, 0);
}

static inline void cpu_cli(void) { __asm__ volatile ("cli" ::: "memory"); }
static inline void cpu_sti(void) { __asm__ volatile ("sti" ::: "memory"); }
static inline void cpu_hlt(void) { __asm__ volatile ("hlt"); }
static inline void cpu_pause(void) { __asm__ volatile ("pause"); }

static inline u32 read_cr0(void) { u32 v; __asm__ volatile ("mov %%cr0, %0" : "=r"(v)); return v; }
static inline u32 read_cr2(void) { u32 v; __asm__ volatile ("mov %%cr2, %0" : "=r"(v)); return v; }
static inline u32 read_cr3(void) { u32 v; __asm__ volatile ("mov %%cr3, %0" : "=r"(v)); return v; }
static inline u32 read_cr4(void) { u32 v; __asm__ volatile ("mov %%cr4, %0" : "=r"(v)); return v; }
static inline u32 read_eflags(void) { u32 v; __asm__ volatile ("pushf; pop %0" : "=r"(v)); return v; }
static inline u32 read_esp(void) { u32 v; __asm__ volatile ("mov %%esp, %0" : "=r"(v)); return v; }
static inline u16 read_cs(void) { u16 v; __asm__ volatile ("mov %%cs, %0" : "=r"(v)); return v; }
static inline u16 read_ds(void) { u16 v; __asm__ volatile ("mov %%ds, %0" : "=r"(v)); return v; }
static inline u16 read_ss(void) { u16 v; __asm__ volatile ("mov %%ss, %0" : "=r"(v)); return v; }

static inline u64 read_tsc(void)
{
    u32 lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

static inline void reboot_machine(void)
{
    /* Pulse the CPU reset line through the keyboard controller, then triple
     * fault as a fallback. */
    for (int i = 0; i < 100; i++) {
        if (!(port_inb(0x64) & 0x02)) break;
    }
    port_outb(0x64, 0xFE);
    for (;;) __asm__ volatile ("cli; hlt");
}

static inline void halt_machine(void)
{
    for (;;) __asm__ volatile ("cli; hlt");
}

#endif /* NOVA_IO_H */
