/* ============================================================================
 * src/apps/devtools.c  -  Kernel explorer for programmers
 *
 * Everything shown here is read live from the kernel: CPUID leaves, heap
 * blocks, IDT gates, PCI configuration space, interrupt counters and the
 * syscall table.  It is deliberately a window into the machine rather than a
 * static documentation page.
 * ============================================================================ */

#include "kernel.h"
#include "theme.h"
#include "gfx.h"
#include "widgets.h"
#include "mouse.h"
#include "timer.h"
#include "memory.h"
#include "interrupts.h"
#include "syscall.h"
#include "pci.h"
#include "cpu.h"
#include "vfs.h"
#include "system.h"
#include "apps.h"
#include "desktop.h"
#include "apps_internal.h"

#define DT_BAR_H      40
#define DT_TABS_H     36
#define DT_LINE_H     20
#define DT_MAX_LINES  96
#define DT_TABS       5

typedef struct {
    char text[168];
    u32  color;
    int  heading;
} dt_line_t;

typedef struct {
    int tab;
    int scroll[DT_TABS];
    pci_device_t pci[PCI_MAX_DEVICES];
    int pci_count;
    int pci_loaded;
    u32 last_refresh;
} devtools_state_t;

static const char *dt_tab_names[DT_TABS] = {
    "CPU", "Memory", "IDT / IRQ", "PCI", "Syscalls"
};

/* ── Line building ────────────────────────────────────────────────────────── */

static int dt_add(dt_line_t *lines, int cap, int *count, u32 color,
                  const char *fmt, ...)
{
    if (*count >= cap) return -1;
    dt_line_t *line = &lines[*count];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kvsnprintf(line->text, sizeof(line->text), fmt, ap);
    __builtin_va_end(ap);
    line->color = color;
    line->heading = 0;
    (*count)++;
    return 0;
}

static int dt_heading(dt_line_t *lines, int cap, int *count, const char *text)
{
    if (*count >= cap) return -1;
    dt_line_t *line = &lines[*count];
    kstrncpy(line->text, text, sizeof(line->text));
    line->color = theme.accent;
    line->heading = 1;
    (*count)++;
    return 0;
}

static void dt_blank(dt_line_t *lines, int cap, int *count)
{
    dt_add(lines, cap, count, theme.text, "");
}

static void dt_wrap(dt_line_t *lines, int cap, int *count, const char *text,
                    u32 color, int columns)
{
    if (columns < 16) columns = 16;
    char buffer[512];
    kstrncpy(buffer, text, sizeof(buffer));
    buffer[sizeof(buffer) - 1] = '\0';

    char line[512];
    int used = 0;
    line[0] = '\0';

    for (int i = 0; buffer[i]; ) {
        if (buffer[i] == '\n') {
            dt_add(lines, cap, count, color, "  %s", line);
            used = 0;
            line[0] = '\0';
            i++;
            continue;
        }

        int word = i;
        while (buffer[word] && buffer[word] != ' ' && buffer[word] != '\n') word++;
        int wlen = word - i;

        if (used > 0 && used + 1 + wlen > columns) {
            dt_add(lines, cap, count, color, "  %s", line);
            used = 0;
            line[0] = '\0';
        }
        if (used > 0) {
            line[used++] = ' ';
            line[used] = '\0';
        }
        if (wlen > columns) wlen = columns;
        for (int k = 0; k < wlen && used < columns + 8; k++)
            line[used++] = buffer[i + k];
        line[used] = '\0';
        i = word;
        while (buffer[i] == ' ') i++;
    }

    if (used) dt_add(lines, cap, count, color, "  %s", line);
}

/* ── Tab content ──────────────────────────────────────────────────────────── */

static void dt_build_cpu(dt_line_t *lines, int cap, int *count)
{
    cpu_info_t cpu;
    cpuid_info(&cpu);

    char features[320];
    cpuid_format_features(features, sizeof(features));

    char brand[64];
    kstrncpy(brand, cpu.brand, sizeof(brand));
    brand[sizeof(brand) - 1] = '\0';

    dt_heading(lines, cap, count, "PROCESSOR");
    dt_add(lines, cap, count, theme.text, "  Brand        %s", brand);
    dt_add(lines, cap, count, theme.text_dim, "  Vendor       %s", cpu.vendor[0] ? cpu.vendor : "unknown");
    dt_add(lines, cap, count, theme.text_dim,
           "  Family %u   Model %u   Stepping %u   Type %u",
           cpu.family, cpu.model, cpu.stepping, cpu.type);
    dt_add(lines, cap, count, theme.text_dim,
           "  CPUID        basic leaves 0x%08x   extended 0x%08x",
           cpu.max_leaf, cpu.max_ext_leaf);
    u64 tsc = read_tsc();
    dt_add(lines, cap, count, theme.text_dim,
           "  TSC          %u:%u", (u32)(tsc >> 32), (u32)tsc);
    dt_blank(lines, cap, count);

    dt_heading(lines, cap, count, "CPUID FEATURE BITS");
    dt_wrap(lines, cap, count, features, theme.success, 88);
    dt_blank(lines, cap, count);

    dt_heading(lines, cap, count, "CONTROL + STATUS REGISTERS");
    dt_add(lines, cap, count, theme.text_dim, "  cr0  0x%08x    cr2  0x%08x", read_cr0(), read_cr2());
    dt_add(lines, cap, count, theme.text_dim, "  cr3  0x%08x    cr4  0x%08x", read_cr3(), read_cr4());
    dt_add(lines, cap, count, theme.text_dim, "  eflags 0x%08x  esp 0x%08x", read_eflags(), read_esp());
    dt_add(lines, cap, count, theme.text_dim,
           "  cs 0x%04x  ds 0x%04x  ss 0x%04x",
           read_cs(), read_ds(), read_ss());
    dt_blank(lines, cap, count);

    dt_heading(lines, cap, count, "SYSTEM");
    dt_add(lines, cap, count, theme.text_dim, "  %s", system_arch());
    dt_add(lines, cap, count, theme.text_dim, "  RAM   %u MiB detected",
           system_total_ram_kb() / 1024);
    dt_add(lines, cap, count, theme.text_dim, "  Heap  %u KiB used of %u KiB",
           mem_used() / 1024, mem_total() / 1024);
    dt_add(lines, cap, count, theme.text_dim,
           "  PIT %u Hz    uptime %u s    syscalls %u",
           TIMER_HZ, timer_seconds(), syscall_count());
}

static void dt_build_memory(dt_line_t *lines, int cap, int *count)
{
    mem_info_t mi;
    mem_info(&mi);

    dt_heading(lines, cap, count, "KERNEL HEAP");
    dt_add(lines, cap, count, theme.text,
           "  Base 0x%08x   Total %u KiB   Used %u KiB   Free %u KiB",
           mi.start, mi.total / 1024, mi.used / 1024, mi.free / 1024);
    dt_add(lines, cap, count, theme.text_dim,
           "  Blocks %u   Largest free %u KiB",
           mi.blocks, mi.largest_free / 1024);
    dt_blank(lines, cap, count);

    dt_heading(lines, cap, count, "VFS PRESSURE");
    dt_add(lines, cap, count, theme.text_dim,
           "  %d / %d nodes used   %u bytes of file data",
           vfs_used_nodes(), VFS_MAX_NODES, vfs_used_bytes());
    dt_blank(lines, cap, count);

    dt_heading(lines, cap, count, "BLOCK MAP");
    int blocks = mem_block_count();
    for (int i = 0; i < blocks; i++) {
        u32 addr, size;
        int free;
        if (mem_block_info(i, &addr, &size, &free) < 0) break;
        dt_add(lines, cap, count, free ? theme.success : theme.text_dim,
               "  [%02d] 0x%08x  %8u B  %s",
               i, addr, size, free ? "free" : "used");
    }
    if (blocks == 0) dt_add(lines, cap, count, theme.text_faint, "  no blocks");
}

static void dt_build_idt(dt_line_t *lines, int cap, int *count)
{
    dt_heading(lines, cap, count, "INTERRUPT DESCRIPTOR TABLE");
    dt_add(lines, cap, count, theme.text,
           "  Base 0x%08x   Limit 0x%04x   Gates 256",
           idt_base(), idt_limit());
    dt_add(lines, cap, count, theme.text_dim,
           "  Total interrupts %u   timer ticks %u",
           interrupt_total_count(), timer_ticks());
    dt_blank(lines, cap, count);

    dt_heading(lines, cap, count, "GATES 0x00 - 0x2F");
    for (int v = 0; v < 48; v++) {
        const char *kind = (v < 32) ? "exception" : "irq";
        dt_add(lines, cap, count, v < 32 ? theme.text_dim : theme.text,
               "  %02x  0x%08x  flags 0x%02x  %s",
               v, idt_gate_offset(v), idt_gate_flags(v), kind);
    }
    dt_blank(lines, cap, count);
    dt_heading(lines, cap, count, "SYSCALL GATE");
    dt_add(lines, cap, count, theme.accent,
           "  80  0x%08x  flags 0x%02x  callable from ring 3",
           idt_gate_offset(0x80), idt_gate_flags(0x80));
    dt_blank(lines, cap, count);

    dt_heading(lines, cap, count, "IRQ COUNTERS");
    for (int irq = 0; irq < 16; irq++) {
        u32 hits = irq_count(irq);
        if (!hits) continue;
        dt_add(lines, cap, count, theme.text_dim, "  IRQ %02d   %u", irq, hits);
    }
    dt_add(lines, cap, count, theme.text_faint,
           "  masks master 0x%02x   slave 0x%02x",
           pic_get_mask_master(), pic_get_mask_slave());
}

static void dt_build_pci(devtools_state_t *d, dt_line_t *lines, int cap, int *count)
{
    if (!d->pci_loaded) {
        d->pci_count = pci_scan(d->pci, PCI_MAX_DEVICES);
        d->pci_loaded = 1;
        d->last_refresh = timer_ticks();
    }

    dt_heading(lines, cap, count, "PCI CONFIGURATION SPACE");
    dt_add(lines, cap, count, theme.text,
           "  %d device%s on buses 0-7", d->pci_count,
           d->pci_count == 1 ? "" : "s");
    dt_blank(lines, cap, count);

    if (d->pci_count == 0) {
        dt_add(lines, cap, count, theme.text_faint, "  No PCI devices answered.");
        return;
    }

    dt_add(lines, cap, count, theme.text_faint,
           "  ADDR      VENDOR:DEVICE   CLASS                     IRQ");
    for (int i = 0; i < d->pci_count; i++) {
        pci_device_t *p = &d->pci[i];
        int irq = pci_read8(p->bus, p->slot, p->func, 0x3C);
        if (irq == 0xFF) irq = 0;
        dt_add(lines, cap, count, theme.text_dim,
               "  %02x:%02x.%x   %04x:%04x     %-22s  %u",
               p->bus, p->slot, p->func, p->vendor, p->device,
               pci_class_name(p->class_code, p->subclass), irq);
    }
    dt_blank(lines, cap, count);
    dt_heading(lines, cap, count, "PROGRAMMING INTERFACES");
    for (int i = 0; i < d->pci_count; i++) {
        pci_device_t *p = &d->pci[i];
        dt_add(lines, cap, count, theme.text_faint,
               "  %02x:%02x.%x  class %02x:%02x  prog-if %02x  header %02x",
               p->bus, p->slot, p->func, p->class_code, p->subclass,
               p->prog_if, p->header_type);
    }
}

static void dt_build_syscalls(dt_line_t *lines, int cap, int *count)
{
    dt_heading(lines, cap, count, "INT 0x80 SYSTEM CALLS");
    dt_add(lines, cap, count, theme.text_dim,
           "  Entry: eax=number  ebx/ecx/edx/esi/edi=arguments");
    dt_add(lines, cap, count, theme.text_dim,
           "  Calls this boot: %u", syscall_count());
    dt_blank(lines, cap, count);

    for (int i = 0; i < SYS_COUNT; i++) {
        const syscall_info_t *info = syscall_info(i);
        if (!info) continue;
        dt_add(lines, cap, count, theme.accent,
               "  %02d  %-12s %d arg%s", i, info->name, info->args,
               info->args == 1 ? "" : "s");
        dt_add(lines, cap, count, theme.text_faint, "      %s", info->description);
    }
}

static int dt_build(devtools_state_t *d, dt_line_t *lines, int cap)
{
    int count = 0;
    switch (d->tab) {
    case 0: dt_build_cpu(lines, cap, &count); break;
    case 1: dt_build_memory(lines, cap, &count); break;
    case 2: dt_build_idt(lines, cap, &count); break;
    case 3: dt_build_pci(d, lines, cap, &count); break;
    case 4: dt_build_syscalls(lines, cap, &count); break;
    default: break;
    }
    return count;
}

/* ── Layout ───────────────────────────────────────────────────────────────── */

static void dt_tab_rect(window_t *w, int index, int *tx, int *ty, int *tw, int *th)
{
    int cw = CWD(w);
    int gap = 6;
    int pad = 12;
    int bw = (cw - pad * 2 - gap * (DT_TABS - 1)) / DT_TABS;
    if (bw < 58) bw = 58;
    if (tx) *tx = pad + index * (bw + gap);
    if (ty) *ty = DT_BAR_H + 6;
    if (tw) *tw = bw;
    if (th) *th = 26;
}

static int dt_visible_rows(window_t *w)
{
    int ch = CHT(w);
    int top = DT_BAR_H + DT_TABS_H + 8;
    int bottom = ch - APP_STATUS_H - 8;
    return max(2, (bottom - top) / DT_LINE_H);
}

static void dt_refresh(devtools_state_t *d)
{
    d->pci_loaded = 0;
    d->last_refresh = timer_ticks();
}

/* ── Drawing ──────────────────────────────────────────────────────────────── */

static void dt_draw(window_t *w)
{
    devtools_state_t *d = (devtools_state_t *)w->state;
    if (!d) return;

    int x = CX(w), y = CY(w), cw = CWD(w), ch = CHT(w);
    gfx_fill(x, y, cw, ch, theme.win);

    /* Header and refresh. */
    gfx_fill(x, y, cw, DT_BAR_H, theme.panel);
    gfx_fill(x, y + DT_BAR_H - 1, cw, 1, theme.border);
    gfx_fill(x + 10, y + 13, 4, 14, theme.accent);
    gfx_text(x + 22, y + 12, "DevTools", theme.text);
    gfx_text(x + 94, y + 12, "kernel explorer", theme.text_faint);

    mouse_state_t *m = mouse_get();
    int mx = m ? m->x : -1;
    int my = m ? m->y : -1;

    int refresh_x = x + cw - 84;
    int refresh_y = y + 8;
    int refresh_hot = app_hit(mx, my, refresh_x, refresh_y, 72, 24);
    gfx_round_rect(refresh_x, refresh_y, 72, 24, 6,
                   refresh_hot ? theme.panel2 : theme.bg1);
    gfx_round_outline(refresh_x, refresh_y, 72, 24, 6, theme.border);
    gfx_text(refresh_x + 16, refresh_y + 4, "Refresh", theme.text_dim);

    /* Tabs. */
    for (int i = 0; i < DT_TABS; i++) {
        int tx, ty, tw, th;
        dt_tab_rect(w, i, &tx, &ty, &tw, &th);
        int ax = x + tx;
        int ay = y + ty;
        int hot = app_hit(mx, my, ax, ay, tw, th);
        int active = (d->tab == i);

        gfx_round_rect(ax, ay, tw, th, 7,
                       active ? theme.accent_soft : (hot ? theme.panel2 : theme.bg1));
        gfx_round_outline(ax, ay, tw, th, 7,
                          active ? theme.accent : theme.border);
        int twidth = gfx_text_width(dt_tab_names[i]);
        gfx_text(ax + (tw - twidth) / 2, ay + 5, dt_tab_names[i],
                 active ? theme.text : theme.text_dim);
    }

    /* Content lines. */
    dt_line_t lines[DT_MAX_LINES];
    int count = dt_build(d, lines, DT_MAX_LINES);
    int rows = dt_visible_rows(w);
    int max_scroll = max(0, count - rows);

    d->scroll[d->tab] = clamp(d->scroll[d->tab], 0, max_scroll);

    int top = y + DT_BAR_H + DT_TABS_H + 8;
    int list_h = rows * DT_LINE_H;
    gfx_fill(x + 8, top, cw - 16, list_h + 4, theme.term_bg);
    gfx_round_rect(x + 8, top, cw - 16, list_h + 4, 8, theme.term_bg);
    gfx_round_outline(x + 8, top, cw - 16, list_h + 4, 8, theme.border);

    gfx_set_clip(x + 12, top + 2, cw - 24, list_h);
    int scroll = d->scroll[d->tab];
    for (int row = 0; row < rows; row++) {
        int index = scroll + row;
        if (index >= count) break;
        int ry = top + 2 + row * DT_LINE_H;
        dt_line_t *line = &lines[index];

        if (line->heading) {
            if (index > scroll)
                gfx_fill(x + 20, ry - 3, cw - 40, 1, theme.border);
            gfx_fill(x + 18, ry + 4, 3, 12, theme.accent);
            gfx_text(x + 27, ry + 2, line->text, line->color);
        } else if (line->text[0]) {
            gfx_text(x + 18, ry + 2, line->text, line->color);
        }
    }
    gfx_reset_clip();

    if (count > rows)
        ui_scrollbar(x + cw - 16, top + 4, 6, list_h - 4, scroll, count, rows);

    /* Status bar. */
    char left[160];
    ksnprintf(left, sizeof(left), "%s  |  %d lines  |  1-5 tabs  R refresh",
              dt_tab_names[d->tab], count);
    char right[64];
    ksnprintf(right, sizeof(right), "%u syscalls", syscall_count());
    app_status(w, left, right, theme.text_faint);
}

/* ── Input ────────────────────────────────────────────────────────────────── */

static void dt_refresh_pressed(devtools_state_t *d)
{
    dt_refresh(d);
    toast_show("DevTools", "Kernel probes refreshed", TOAST_SUCCESS);
}

static void dt_mouse(window_t *w, int mx, int my, int button, int pressed)
{
    devtools_state_t *d = (devtools_state_t *)w->state;
    if (!d || button != MOUSE_BTN_LEFT || !pressed) return;

    int cw = CWD(w);
    if (app_hit(mx, my, cw - 84, 8, 72, 24)) {
        dt_refresh_pressed(d);
        return;
    }

    for (int i = 0; i < DT_TABS; i++) {
        int tx, ty, tw, th;
        dt_tab_rect(w, i, &tx, &ty, &tw, &th);
        if (app_hit(mx, my, tx, ty, tw, th)) {
            d->tab = i;
            return;
        }
    }
}

static void dt_key(window_t *w, const key_event_t *ev)
{
    devtools_state_t *d = (devtools_state_t *)w->state;
    if (!d) return;

    int rows = dt_visible_rows(w);
    int *scroll = &d->scroll[d->tab];

    if (ev->ch >= '1' && ev->ch <= '5') {
        d->tab = ev->ch - '1';
        return;
    }

    switch (ev->key) {
    case KEY_UP:
        if (*scroll > 0) (*scroll)--;
        return;
    case KEY_DOWN:
        (*scroll)++;
        return;
    case KEY_PAGEUP:
        *scroll -= rows;
        if (*scroll < 0) *scroll = 0;
        return;
    case KEY_PAGEDOWN:
        *scroll += rows;
        return;
    case KEY_HOME:
        *scroll = 0;
        return;
    case KEY_END:
        *scroll = 9999;
        return;
    default:
        break;
    }

    if (ev->ch == 'r' || ev->ch == 'R') dt_refresh_pressed(d);
}

static void dt_wheel(window_t *w, int delta, int x, int y)
{
    devtools_state_t *d = (devtools_state_t *)w->state;
    UNUSED(x); UNUSED(y);
    if (!d) return;
    d->scroll[d->tab] += delta > 0 ? -2 : 2;
    if (d->scroll[d->tab] < 0) d->scroll[d->tab] = 0;
}

static void dt_close(window_t *w)
{
    if (w->state) {
        kfree(w->state);
        w->state = NULL;
    }
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

void app_devtools_open(void)
{
    window_t *w = wm_find(APP_DEVTOOLS);
    if (w) {
        wm_focus(w);
        return;
    }

    devtools_state_t *d = (devtools_state_t *)kzalloc(sizeof(devtools_state_t));
    if (!d) {
        toast_show("DevTools", "Not enough memory", TOAST_ERROR);
        return;
    }
    d->tab = 0;

    int ww = min(960, max(660, gfx.width - 140));
    int wh = min(650, max(430, gfx.height - 150));
    int wx = max(24, (gfx.width - ww) / 2 + 10);
    int wy = max(50, (gfx.height - wh) / 2 - 8);

    window_t *win = wm_create(APP_DEVTOOLS, "DevTools", wx, wy, ww, wh,
                              640, 400, dt_draw, dt_key, dt_mouse,
                              dt_wheel, dt_close, d);
    if (!win) kfree(d);
}
