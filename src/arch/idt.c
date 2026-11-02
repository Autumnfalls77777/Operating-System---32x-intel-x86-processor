/* ============================================================================
 * src/arch/idt.c  -  Interrupt Descriptor Table and 8259 PIC
 * ============================================================================ */

#include "kernel.h"
#include "interrupts.h"
#include "syscall.h"
#include "timer.h"
#include "keyboard.h"

#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1

#define ICW1_INIT  0x11
#define ICW4_8086  0x01

typedef struct PACKED {
    u16 base_lo;
    u16 selector;
    u8  zero;
    u8  flags;
    u16 base_hi;
} idt_entry_t;

typedef struct PACKED {
    u16 limit;
    u32 base;
} idt_ptr_t;

static idt_entry_t idt[256];
static idt_ptr_t   idtr;

static u8  master_mask = 0xFF;
static u8  slave_mask  = 0xFF;
static u32 irq_counts[16];
static u32 total_interrupts;

extern u32 isr_stub_table[256];

void idt_init(void)
{
    for (int i = 0; i < 256; i++) {
        u32 handler = isr_stub_table[i];
        idt[i].base_lo  = (u16)(handler & 0xFFFF);
        idt[i].selector = 0x08;
        idt[i].zero     = 0;
        idt[i].flags    = 0x8E;              /* present, ring 0, 32-bit gate */
        idt[i].base_hi  = (u16)((handler >> 16) & 0xFFFF);
    }

    /* Allow ring 3 to call the syscall gate once user mode exists. */
    idt[0x80].flags = 0xEE;

    idtr.limit = (u16)(sizeof(idt) - 1);
    idtr.base  = (u32)&idt[0];
    __asm__ volatile ("lidt %0" : : "m"(idtr));

    total_interrupts = 0;
    for (int i = 0; i < 16; i++) irq_counts[i] = 0;
}

u32 idt_base(void)  { return idtr.base; }
u32 idt_limit(void) { return idtr.limit; }

u32 idt_gate_offset(int vector)
{
    if (vector < 0 || vector > 255) return 0;
    return (u32)idt[vector].base_lo | ((u32)idt[vector].base_hi << 16);
}

u8 idt_gate_flags(int vector)
{
    if (vector < 0 || vector > 255) return 0;
    return idt[vector].flags;
}

static void pic_write(u16 port, u8 value)
{
    port_outb(port, value);
    io_wait();
}

void pic_init(void)
{
    /* ICW1: start initialisation, expect ICW4 */
    pic_write(PIC1_CMD, ICW1_INIT);
    pic_write(PIC2_CMD, ICW1_INIT);

    /* ICW2: vector offsets */
    pic_write(PIC1_DATA, 0x20);
    pic_write(PIC2_DATA, 0x28);

    /* ICW3: master has slave on IRQ2; slave is cascaded on IRQ2 */
    pic_write(PIC1_DATA, 0x04);
    pic_write(PIC2_DATA, 0x02);

    /* ICW4: 8086 mode */
    pic_write(PIC1_DATA, ICW4_8086);
    pic_write(PIC2_DATA, ICW4_8086);

    pic_mask_all();
}

void pic_mask_all(void)
{
    master_mask = 0xFF;
    slave_mask  = 0xFF;
    port_outb(PIC1_DATA, master_mask);
    port_outb(PIC2_DATA, slave_mask);
}

void pic_unmask(u8 irq)
{
    if (irq < 8) {
        master_mask &= (u8)~(1u << irq);
        port_outb(PIC1_DATA, master_mask);
    } else if (irq < 16) {
        slave_mask &= (u8)~(1u << (irq - 8));
        port_outb(PIC2_DATA, slave_mask);
        /* Cascade IRQ2 must be enabled for slave interrupts. */
        master_mask &= (u8)~(1u << 2);
        port_outb(PIC1_DATA, master_mask);
    }
}

void pic_mask(u8 irq)
{
    if (irq < 8) {
        master_mask |= (u8)(1u << irq);
        port_outb(PIC1_DATA, master_mask);
    } else if (irq < 16) {
        slave_mask |= (u8)(1u << (irq - 8));
        port_outb(PIC2_DATA, slave_mask);
    }
}

u8 pic_get_mask_master(void) { return master_mask; }
u8 pic_get_mask_slave(void)  { return slave_mask; }

void pic_send_eoi(u8 irq)
{
    if (irq >= 8) port_outb(PIC2_CMD, 0x20);
    port_outb(PIC1_CMD, 0x20);
}

u32 irq_count(int irq)
{
    if (irq < 0 || irq > 15) return 0;
    return irq_counts[irq];
}

u32 interrupt_total_count(void) { return total_interrupts; }

void isr_dispatch(regs_t *r)
{
    total_interrupts++;

    if (r->vector < 32) {
        panic_regs(r, "CPU exception");
    }

    if (r->vector == 0x80) {
        syscall_handler(r);
        return;
    }

    if (r->vector >= 32 && r->vector < 48) {
        u8 irq = (u8)(r->vector - 32);
        irq_counts[irq]++;

        switch (irq) {
        case 0:
            timer_irq();
            break;
        case 1:
        case 12:
            /* IRQ1 and IRQ12 share the PS/2 controller.  Drain every byte and
             * route it by the AUX status bit. */
            keyboard_poll_raw();
            break;
        case 2:
            /* Cascade: the slave EOI below handles it. */
            break;
        default:
            break;
        }

        if (irq >= 8) port_outb(PIC2_CMD, 0x20);
        port_outb(PIC1_CMD, 0x20);
        return;
    }

    /* Unknown software interrupt: ignore. */
}
