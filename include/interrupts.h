/* ============================================================================
 * include/interrupts.h  -  IDT, PIC and interrupt dispatch
 * ============================================================================ */

#ifndef NOVA_INTERRUPTS_H
#define NOVA_INTERRUPTS_H

#include "types.h"

/* Register frame pushed by isr_common (see src/arch/interrupts.asm). */
typedef struct regs {
    u32 gs, fs, es, ds;
    u32 edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    u32 vector, error;
    u32 eip, cs, eflags;
} regs_t;

void idt_init(void);
void pic_init(void);
void pic_mask_all(void);
void pic_unmask(u8 irq);
void pic_mask(u8 irq);
u8   pic_get_mask_master(void);
u8   pic_get_mask_slave(void);
void pic_send_eoi(u8 irq);

void isr_dispatch(regs_t *r);

u32  idt_base(void);
u32  idt_limit(void);
u32  idt_gate_offset(int vector);
u8   idt_gate_flags(int vector);

u32  irq_count(int irq);
u32  interrupt_total_count(void);

#endif /* NOVA_INTERRUPTS_H */
