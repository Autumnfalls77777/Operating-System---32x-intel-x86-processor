/* ============================================================================
 * src/kernel/panic.c  -  Fatal error screen and register dump
 * ============================================================================ */

#include "kernel.h"
#include "gfx.h"
#include "interrupts.h"
#include "serial.h"
#include "timer.h"

static const char *exception_names[32] = {
    "Divide by zero", "Debug", "NMI", "Breakpoint",
    "Overflow", "Bound range", "Invalid opcode", "Device not available",
    "Double fault", "Coprocessor segment", "Invalid TSS", "Segment not present",
    "Stack fault", "General protection fault", "Page fault", "Reserved",
    "x87 floating point", "Alignment check", "Machine check", "SIMD floating point",
    "Virtualization", "Control protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor", "VMM communication", "Security", "Reserved"
};

static void panic_screen(regs_t *r, const char *msg)
{
    gfx_target_back();
    gfx_reset_clip();
    gfx_gradient_v(0, 0, gfx.width, gfx.height, 0x1B0508, 0x07030A);

    int cw = min(gfx.width - 80, 900);
    int ch = min(gfx.height - 80, 560);
    int cx = (gfx.width - cw) / 2;
    int cy = (gfx.height - ch) / 2;

    gfx_shadow(cx, cy, cw, ch, 18, 18, 90);
    gfx_round_rect(cx, cy, cw, ch, 18, 0x120A12);
    gfx_round_outline(cx, cy, cw, ch, 18, 0x7F1D1D);

    gfx_fill(cx + 20, cy + 20, 4, 44, 0xF87171);
    gfx_text_scale(cx + 38, cy + 18, "KERNEL PANIC", 0xFCA5A5, 3);
    gfx_text(cx + 40, cy + 72, "The kernel hit a fatal error and halted safely.", 0xC9A5A5);

    gfx_text(cx + 40, cy + 110, msg, 0xFDE68A);
    if (r) {
        int rx = cx + 40;
        int ry = cy + 150;
        gfx_textf(rx, ry, 0xE5E7EB, "vector  0x%02x  %s", r->vector,
                  r->vector < 32 ? exception_names[r->vector] : "software interrupt");
        gfx_textf(rx, ry + 20, 0xE5E7EB, "error   0x%08x    eip 0x%08x    eflags 0x%08x",
                  r->error, r->eip, r->eflags);
        gfx_textf(rx, ry + 40, 0xE5E7EB, "cs      0x%04x        cr0 0x%08x    cr2 0x%08x",
                  r->cs, read_cr0(), read_cr2());
        gfx_textf(rx, ry + 60, 0xE5E7EB, "cr3     0x%08x        esp 0x%08x    ebp 0x%08x",
                  read_cr3(), r->esp_dummy, r->ebp);

        int gy = ry + 100;
        gfx_text(rx, gy, "GENERAL REGISTERS", 0xF87171);
        gfx_textf(rx, gy + 22, 0xD1D5DB, "eax %08x   ebx %08x   ecx %08x   edx %08x",
                  r->eax, r->ebx, r->ecx, r->edx);
        gfx_textf(rx, gy + 42, 0xD1D5DB, "esi %08x   edi %08x   ebp %08x   esp %08x",
                  r->esi, r->edi, r->ebp, r->esp_dummy);
        gfx_textf(rx, gy + 62, 0xD1D5DB, " ds %04x      es %04x      fs %04x      gs %04x",
                  r->ds, r->es, r->fs, r->gs);
    }

    gfx_text(cx + 40, cy + ch - 46, "System halted. Press R to reboot, or close the QEMU window.",
             0x9CA3AF);
    gfx_text(cx + 40, cy + ch - 26, "Full register state was also written to the serial log.",
             0x6B7280);
    gfx_present();
}

static void panic_common(regs_t *r, const char *msg) NORETURN;
static void panic_common(regs_t *r, const char *msg)
{
    cpu_cli();

    serial_write("\n*** NOVA KERNEL PANIC ***\n");
    serial_printf("%s\n", msg);
    if (r) {
        serial_printf("vector=%02x error=%08x eip=%08x cs=%04x eflags=%08x\n",
                      r->vector, r->error, r->eip, r->cs, r->eflags);
        serial_printf("eax=%08x ebx=%08x ecx=%08x edx=%08x\n",
                      r->eax, r->ebx, r->ecx, r->edx);
        serial_printf("esi=%08x edi=%08x ebp=%08x esp=%08x\n",
                      r->esi, r->edi, r->ebp, r->esp_dummy);
        serial_printf("cr0=%08x cr2=%08x cr3=%08x\n", read_cr0(), read_cr2(), read_cr3());
    }

    if (gfx.ready) panic_screen(r, msg);

    /* Poll the PS/2 controller directly (interrupts are off). */
    for (;;) {
        if (port_inb(0x64) & 0x01) {
            u8 sc = port_inb(0x60);
            if (sc == 0x13 || sc == 0x19) reboot_machine();
        }
        cpu_pause();
    }
}

void panic(const char *fmt, ...)
{
    char msg[512];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kvsnprintf(msg, sizeof(msg), fmt, ap);
    __builtin_va_end(ap);
    panic_common(NULL, msg);
}

void panic_regs(regs_t *r, const char *msg)
{
    char full[512];
    if (r && r->vector < 32)
        ksnprintf(full, sizeof(full), "%s: %s (vector %u, error 0x%x)",
                  msg, exception_names[r->vector], r->vector, r->error);
    else
        ksnprintf(full, sizeof(full), "%s", msg);
    panic_common(r, full);
}
