/* ============================================================================
 * src/apps/help.c  -  Help and developer guide
 * ============================================================================ */

#include "kernel.h"
#include "theme.h"
#include "gfx.h"
#include "widgets.h"
#include "mouse.h"
#include "timer.h"
#include "memory.h"
#include "apps.h"
#include "desktop.h"
#include "apps_internal.h"
#include "syscall.h"

#define HELP_SIDEBAR_W 172
#define HELP_BAR_H     40
#define HELP_LINE_H    22
#define HELP_MAX_LINES 220
#define HELP_CHAPTERS  5

typedef struct {
    char text[240];
    char key[40];
    u8   kind;
} help_line_t;

enum {
    HELP_H1 = 0,
    HELP_H2,
    HELP_BODY,
    HELP_KEY,
    HELP_CODE,
    HELP_BULLET,
    HELP_GAP
};

typedef struct {
    int chapter;
    int scroll[HELP_CHAPTERS];
} help_state_t;

static const char *help_chapters[HELP_CHAPTERS] = {
    "Getting started", "Keyboard & mouse", "Shell", "Kernel", "Syscalls"
};

/* â”€â”€ Line building â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void help_add(help_line_t *lines, int cap, int *count, int kind,
                     const char *key, const char *text)
{
    if (*count >= cap) return;
    help_line_t *line = &lines[*count];
    line->kind = (u8)kind;
    line->key[0] = '\0';
    line->text[0] = '\0';
    if (key) kstrncpy(line->key, key, sizeof(line->key));
    if (text) kstrncpy(line->text, text, sizeof(line->text));
    (*count)++;
}

static void help_gap(help_line_t *lines, int cap, int *count)
{
    help_add(lines, cap, count, HELP_GAP, NULL, NULL);
}

static void help_body(help_line_t *lines, int cap, int *count, const char *text,
                      int columns)
{
    if (columns < 20) columns = 20;

    char buffer[512];
    kstrncpy(buffer, text, sizeof(buffer));
    buffer[sizeof(buffer) - 1] = '\0';

    char line[300];
    int used = 0;
    line[0] = '\0';

    for (int i = 0; buffer[i]; ) {
        int word = i;
        while (buffer[word] && buffer[word] != ' ') word++;
        int wlen = word - i;

        if (used > 0 && used + 1 + wlen > columns) {
            help_add(lines, cap, count, HELP_BODY, NULL, line);
            used = 0;
            line[0] = '\0';
        }
        if (used > 0) {
            line[used++] = ' ';
            line[used] = '\0';
        }
        if (wlen > columns) wlen = columns;
        for (int k = 0; k < wlen && used < (int)sizeof(line) - 1; k++)
            line[used++] = buffer[i + k];
        line[used] = '\0';
        i = word;
        while (buffer[i] == ' ') i++;
    }

    if (used) help_add(lines, cap, count, HELP_BODY, NULL, line);
}

static void help_bullet(help_line_t *lines, int cap, int *count, const char *text)
{
    help_add(lines, cap, count, HELP_BULLET, NULL, text);
}

static void help_code(help_line_t *lines, int cap, int *count, const char *text)
{
    help_add(lines, cap, count, HELP_CODE, NULL, text);
}

static void help_key_row(help_line_t *lines, int cap, int *count,
                     const char *key, const char *text)
{
    help_add(lines, cap, count, HELP_KEY, key, text);
}

/* â”€â”€ Chapter content â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void help_build_start(help_line_t *lines, int cap, int *count, int cols)
{
    help_add(lines, cap, count, HELP_H1, NULL, "Welcome to Altitude OS");
    help_body(lines, cap, count,
              "NOVA is a compact 32-bit x86 operating system designed to be read, "
              "modified and extended. The whole kernel is small enough to understand "
              "in an afternoon, yet it boots through a real BIOS path, programs a VBE "
              "framebuffer, runs an interrupt-driven desktop and exposes every part of "
              "itself to you.", cols);
    help_gap(lines, cap, count);

    help_add(lines, cap, count, HELP_H2, NULL, "Your first five minutes");
    help_key_row(lines, cap, count, "Super", "Open the application launcher");
    help_key_row(lines, cap, count, "Enter", "Run the current terminal command");
    help_key_row(lines, cap, count, "F1", "Open this guide from anywhere");
    help_key_row(lines, cap, count, "Esc", "Close the launcher, menus and notifications");
    help_gap(lines, cap, count);

    help_body(lines, cap, count,
              "Open Terminal from the dock and try a few commands. Everything runs in "
              "the same address space, so shell commands can inspect the same kernel "
              "structures that the desktop uses.", cols);
    help_code(lines, cap, count, "neofetch");
    help_code(lines, cap, count, "cat /proc/cpuinfo");
    help_code(lines, cap, count, "ls /home/dev");
    help_code(lines, cap, count, "edit /home/dev/welcome.txt");
    help_gap(lines, cap, count);

    help_add(lines, cap, count, HELP_H2, NULL, "The bundled applications");
    help_bullet(lines, cap, count, "Terminal - interactive shell with history and completion");
    help_bullet(lines, cap, count, "Files - browse, create, delete and edit VFS nodes");
    help_bullet(lines, cap, count, "Editor - line numbers, syntax tinting and Ctrl+S save");
    help_bullet(lines, cap, count, "System Monitor - heap and interrupt history graphs");
    help_bullet(lines, cap, count, "Process Manager - PCBs and four scheduling policies");
    help_bullet(lines, cap, count, "Calculator - integer expressions with precedence");
    help_bullet(lines, cap, count, "DevTools - live CPUID, IDT, PCI and syscall probes");
    help_bullet(lines, cap, count, "Settings - accent, wallpaper, desktop and power");
    help_gap(lines, cap, count);

    help_add(lines, cap, count, HELP_H2, NULL, "Everything is in memory");
    help_body(lines, cap, count,
              "The filesystem is a RAM filesystem. Files survive window changes but not "
              "a reboot. Use the Editor or shell redirection to create files, and use "
              "the VFS tree as a scratchpad for experiments.", cols);
}

static void help_build_keys(help_line_t *lines, int cap, int *count, int cols)
{
    help_add(lines, cap, count, HELP_H1, NULL, "Keyboard and mouse");
    help_body(lines, cap, count,
              "The desktop is built for a keyboard as much as a mouse. Global "
              "shortcuts are handled by the compositor before the focused window sees "
              "a key.", cols);
    help_gap(lines, cap, count);

    help_add(lines, cap, count, HELP_H2, NULL, "Desktop");
    help_key_row(lines, cap, count, "Super", "Toggle the application launcher");
    help_key_row(lines, cap, count, "Esc", "Close the launcher, control center or menu");
    help_key_row(lines, cap, count, "Alt+Tab", "Cycle focus to the next window");
    help_key_row(lines, cap, count, "Shift+Alt+Tab", "Cycle focus to the previous window");
    help_key_row(lines, cap, count, "F1", "Open Help");
    help_key_row(lines, cap, count, "Right click", "Open the desktop context menu");
    help_gap(lines, cap, count);

    help_body(lines, cap, count,
              "Drag a title bar to move a window. Drag any edge or corner to resize. "
              "The three title-bar buttons minimize, maximize and close. The dock "
              "restores minimized windows and clicking the focused app minimizes it.",
              cols);
    help_gap(lines, cap, count);

    help_add(lines, cap, count, HELP_H2, NULL, "Terminal");
    help_key_row(lines, cap, count, "Ctrl+L", "Clear the scrollback");
    help_key_row(lines, cap, count, "Ctrl+C", "Cancel the current input line");
    help_key_row(lines, cap, count, "Ctrl+U", "Erase the input line");
    help_key_row(lines, cap, count, "Ctrl+A / Ctrl+E", "Jump to start or end of the line");
    help_key_row(lines, cap, count, "Ctrl+W", "Delete the previous word");
    help_key_row(lines, cap, count, "Tab", "Complete a path");
    help_key_row(lines, cap, count, "Up / Down", "Walk command history");
    help_key_row(lines, cap, count, "PgUp / PgDn", "Scroll the terminal buffer");
    help_gap(lines, cap, count);

    help_add(lines, cap, count, HELP_H2, NULL, "Editor");
    help_key_row(lines, cap, count, "Ctrl+S", "Save the current file");
    help_key_row(lines, cap, count, "Ctrl+R", "Reload from the VFS");
    help_key_row(lines, cap, count, "Tab", "Insert four spaces");
    help_key_row(lines, cap, count, "PgUp / PgDn", "Move a screenful");
    help_gap(lines, cap, count);

    help_add(lines, cap, count, HELP_H2, NULL, "Process Manager");
    help_key_row(lines, cap, count, "S", "Spawn a worker process");
    help_key_row(lines, cap, count, "K", "Kill the selected process");
    help_key_row(lines, cap, count, "T", "Advance the scheduler one tick");
    help_key_row(lines, cap, count, "A", "Toggle automatic scheduling");
    help_key_row(lines, cap, count, "1 - 4", "Select FCFS, RR, Priority or SJF");
}

static void help_build_shell(help_line_t *lines, int cap, int *count, int cols)
{
    help_add(lines, cap, count, HELP_H1, NULL, "The NOVA shell");
    help_body(lines, cap, count,
              "The shell resolves paths against a current directory, expands nothing, "
              "and supports quoted arguments plus output redirection. Type help inside "
              "the terminal for the compact command list.", cols);
    help_gap(lines, cap, count);

    help_add(lines, cap, count, HELP_H2, NULL, "Navigation and files");
    help_code(lines, cap, count, "pwd / cd / ls / tree");
    help_code(lines, cap, count, "cat / touch / mkdir / rm / rmdir");
    help_code(lines, cap, count, "mv / cp / chmod / grep / hexdump / wc");
    help_gap(lines, cap, count);

    help_add(lines, cap, count, HELP_H2, NULL, "System inspection");
    help_code(lines, cap, count, "uname / uptime / date / free / meminfo");
    help_code(lines, cap, count, "ps / spawn / kill / sched / tick");
    help_code(lines, cap, count, "dmesg / lspci / cpuinfo / idt / syscalls");
    help_code(lines, cap, count, "neofetch / about / apps / open <app>");
    help_gap(lines, cap, count);

    help_add(lines, cap, count, HELP_H2, NULL, "Redirection and scripts");
    help_body(lines, cap, count,
              "A command may write its output to a file. A single > truncates and a "
              "double >> appends. Quoted paths and arguments are preserved.", cols);
    help_code(lines, cap, count, "echo hello > /home/dev/hello.txt");
    help_code(lines, cap, count, "cat /proc/meminfo >> /home/dev/report.txt");
    help_code(lines, cap, count, "sh /home/dev/hello.sh");
    help_body(lines, cap, count,
              "Scripts are ordinary files: one command per line. `exit` closes the "
              "terminal window; it also ends a nested script.", cols);
    help_gap(lines, cap, count);

    help_add(lines, cap, count, HELP_H2, NULL, "Try this");
    help_code(lines, cap, count, "echo NOVA > /home/dev/notes.txt");
    help_code(lines, cap, count, "edit /home/dev/notes.txt");
    help_code(lines, cap, count, "sched priority");
    help_code(lines, cap, count, "spawn compiler 2 12");
    help_code(lines, cap, count, "tick 20");
    help_code(lines, cap, count, "dmesg");
}

static void help_build_kernel(help_line_t *lines, int cap, int *count, int cols)
{
    help_add(lines, cap, count, HELP_H1, NULL, "Inside the kernel");
    help_body(lines, cap, count,
              "NOVA boots from a two-stage BIOS loader. Stage 1 reads 8 KiB of stage 2, "
              "which collects E820 memory, asks for a display resolution, programs a "
              "VBE linear framebuffer and jumps to the flat 32-bit kernel at 0x10000.",
              cols);
    help_gap(lines, cap, count);

    help_add(lines, cap, count, HELP_H2, NULL, "Memory map");
    help_code(lines, cap, count, "0x00005000  boot_info_t handed over by stage 2");
    help_code(lines, cap, count, "0x00010000  kernel image (.text/.rodata/.data/.bss)");
    help_code(lines, cap, count, "0x01000000  cached wallpaper buffer (16 MiB)");
    help_code(lines, cap, count, "0x02000000  compositor back-buffer (16 MiB)");
    help_code(lines, cap, count, "0x04000000  kernel heap (32 MiB)");
    help_gap(lines, cap, count);

    help_add(lines, cap, count, HELP_H2, NULL, "Subsystems");
    help_bullet(lines, cap, count, "arch/ - GDT, IDT, ISR stubs, 8259 PIC and INT 0x80");
    help_bullet(lines, cap, count, "drivers/ - PS/2 keyboard and mouse, PIT, RTC, PCI, CPUID");
    help_bullet(lines, cap, count, "mm/ - boundary-tag heap with a first-fit free list");
    help_bullet(lines, cap, count, "fs/ - hierarchical VFS, heap-backed files and /proc");
    help_bullet(lines, cap, count, "proc/ - process control blocks and the ready queue");
    help_bullet(lines, cap, count, "gui/ - software compositor, theme, widgets and windows");
    help_bullet(lines, cap, count, "apps/ - shell, editor, files, monitor and tools");
    help_gap(lines, cap, count);

    help_add(lines, cap, count, HELP_H2, NULL, "From shell to kernel");
    help_body(lines, cap, count,
              "The shell calls the same C functions the desktop uses. There is no user "
              "mode yet, so a developer can follow a path directly from a command to a "
              "device register. INT 0x80 is wired and ready for ring 3 work.", cols);
    help_code(lines, cap, count, "u32 ticks = timer_ticks();");
    help_code(lines, cap, count, "int node = vfs_lookup(\"/proc/meminfo\", \"/\");");
    help_code(lines, cap, count, "sys_call(SYS_UPTIME, 0, 0, 0, 0, 0);");
    help_gap(lines, cap, count);

    help_add(lines, cap, count, HELP_H2, NULL, "Build and run");
    help_code(lines, cap, count, "make all");
    help_code(lines, cap, count, "make run             # 1024x768 window");
    help_code(lines, cap, count, "make run-fullscreen  # native fullscreen");
    help_code(lines, cap, count, "make run-scaled      # scaled window");
    help_code(lines, cap, count, "make clean");
    help_gap(lines, cap, count);
    help_body(lines, cap, count,
              "The kernel must remain under 448 KiB because the bootloader reads a "
              "fixed number of floppy sectors. The Makefile checks the size and stops "
              "if the image grows too large.", cols);
}

static void help_build_syscalls(help_line_t *lines, int cap, int *count, int cols)
{
    help_add(lines, cap, count, HELP_H1, NULL, "System call interface");
    help_body(lines, cap, count,
              "Software interrupt 0x80 is the stable boundary between programs and the "
              "kernel. Arguments travel in ebx, ecx, edx, esi and edi; the result is "
              "returned in eax. The table below is generated from the live kernel table.",
              cols);
    help_gap(lines, cap, count);

    help_add(lines, cap, count, HELP_H2, NULL, "Live table");
    for (int i = 0; i < SYS_COUNT; i++) {
        const syscall_info_t *info = syscall_info(i);
        if (!info) continue;
        char text[200];
        ksnprintf(text, sizeof(text), "%s  (%d arg%s)", info->description,
                  info->args, info->args == 1 ? "" : "s");
        char key[40];
        ksnprintf(key, sizeof(key), "%02d %s", i, info->name);
        help_key_row(lines, cap, count, key, text);
    }
    help_gap(lines, cap, count);

    help_add(lines, cap, count, HELP_H2, NULL, "Calling from C");
    help_code(lines, cap, count, "u32 ms = sys_call(SYS_UPTIME, 0, 0, 0, 0, 0);");
    help_code(lines, cap, count, "u32 pid = sys_call(SYS_GETPID, 0, 0, 0, 0, 0);");
    help_code(lines, cap, count, "sys_call(SYS_WRITE, 2, (u32)\"hi\\n\", 3, 0, 0);");
    help_gap(lines, cap, count);
    help_body(lines, cap, count,
              "The interrupt gate already carries DPL 3, so the same table can be used "
              "unchanged when a user-mode loader is added.", cols);
}

static int help_build(help_state_t *h, help_line_t *lines, int cap, int columns)
{
    int count = 0;
    switch (h->chapter) {
    case 0: help_build_start(lines, cap, &count, columns); break;
    case 1: help_build_keys(lines, cap, &count, columns); break;
    case 2: help_build_shell(lines, cap, &count, columns); break;
    case 3: help_build_kernel(lines, cap, &count, columns); break;
    case 4: help_build_syscalls(lines, cap, &count, columns); break;
    default: break;
    }
    return count;
}

/* â”€â”€ Layout â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void help_sidebar_rect(window_t *w, int index, int *x, int *y, int *bw, int *bh)
{
    if (x)  *x  = CX(w) + 8;
    if (y)  *y  = CY(w) + 50 + index * 42;
    if (bw) *bw = HELP_SIDEBAR_W - 16;
    if (bh) *bh = 36;
}

static int help_visible_rows(window_t *w)
{
    int ch = CHT(w);
    int top = HELP_BAR_H + 16;
    int bottom = ch - APP_STATUS_H - 8;
    return max(3, (bottom - top) / HELP_LINE_H);
}

/* â”€â”€ Drawing â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void help_draw(window_t *w)
{
    help_state_t *h = (help_state_t *)w->state;
    if (!h) return;

    int x = CX(w), y = CY(w), cw = CWD(w), ch = CHT(w);
    gfx_fill(x, y, cw, ch, theme.win);

    /* Header. */
    gfx_fill(x, y, cw, HELP_BAR_H, theme.panel);
    gfx_fill(x, y + HELP_BAR_H - 1, cw, 1, theme.border);
    gfx_fill(x + 10, y + 13, 4, 14, theme.accent);
    gfx_text(x + 22, y + 12, "Help", theme.text);
    gfx_text(x + 70, y + 12, "NOVA developer guide", theme.text_faint);
    gfx_text_right(x + cw - 14, y + 12, "F1", theme.accent);

    mouse_state_t *m = mouse_get();
    int mx = m ? m->x : -1;
    int my = m ? m->y : -1;

    /* Sidebar. */
    int side_y = y + HELP_BAR_H;
    int side_h = ch - HELP_BAR_H - APP_STATUS_H;
    gfx_fill(x, side_y, HELP_SIDEBAR_W, side_h, theme.panel);
    gfx_fill(x + HELP_SIDEBAR_W, side_y, 1, side_h, theme.border);
    gfx_text(x + 18, side_y + 12, "CHAPTERS", theme.text_faint);

    for (int i = 0; i < HELP_CHAPTERS; i++) {
        int bx, by, bw, bh;
        help_sidebar_rect(w, i, &bx, &by, &bw, &bh);
        (void)bw; (void)bh;
        int selected = (h->chapter == i);
        int hot = app_hit(mx, my, x + 8, by, HELP_SIDEBAR_W - 16, 36);

        if (selected)
            gfx_round_rect(x + 8, by, HELP_SIDEBAR_W - 16, 36, 8, theme.accent_soft);
        else if (hot)
            gfx_round_rect(x + 8, by, HELP_SIDEBAR_W - 16, 36, 8, theme.panel2);

        gfx_text(x + 20, by + 10, help_chapters[i],
                 selected ? theme.text : theme.text_dim);
        if (selected)
            gfx_fill(x + 8, by + 8, 3, 20, theme.accent);
    }

    gfx_fill(x + 14, side_y + side_h - 60, HELP_SIDEBAR_W - 28, 1, theme.border);
    gfx_text(x + 18, side_y + side_h - 46, "Tip", theme.accent);
    gfx_text(x + 18, side_y + side_h - 28, "F1 opens this guide.", theme.text_faint);

    /* Content. */
    int cx = x + HELP_SIDEBAR_W + 20;
    int cy = y + HELP_BAR_H + 12;
    int ccw = cw - HELP_SIDEBAR_W - 40;
    int cch = ch - HELP_BAR_H - APP_STATUS_H - 20;
    int columns = max(20, ccw / 8 - 2);

    help_line_t lines[HELP_MAX_LINES];
    int count = help_build(h, lines, HELP_MAX_LINES, columns);
    int rows = help_visible_rows(w);
    int max_scroll = max(0, count - rows);
    h->scroll[h->chapter] = clamp(h->scroll[h->chapter], 0, max_scroll);
    int scroll = h->scroll[h->chapter];

    gfx_set_clip(cx, cy, ccw, cch);
    for (int row = 0; row < rows; row++) {
        int index = scroll + row;
        if (index >= count) break;
        help_line_t *line = &lines[index];
        int ly = cy + row * HELP_LINE_H;

        switch (line->kind) {
        case HELP_H1:
            gfx_fill(cx, ly + 4, 3, 14, theme.accent);
            gfx_text(cx + 12, ly + 2, line->text, theme.text);
            break;

        case HELP_H2:
            gfx_fill(cx, ly + 6, 10, 2, theme.accent2);
            gfx_text(cx + 18, ly + 2, line->text, theme.accent);
            break;

        case HELP_BODY:
            gfx_text(cx + 6, ly + 2, line->text, theme.text_dim);
            break;

        case HELP_BULLET:
            gfx_circle_fill(cx + 9, ly + 10, 3, theme.accent);
            gfx_text(cx + 20, ly + 2, line->text, theme.text_dim);
            break;

        case HELP_CODE:
            gfx_round_rect(cx + 4, ly + 1, ccw - 10, 19, 5, theme.term_bg);
            gfx_text(cx + 14, ly + 3, line->text, theme.accent2);
            break;

        case HELP_KEY: {
            int kw = gfx_text_width(line->key) + 12;
            gfx_round_rect(cx + 6, ly + 1, kw, 20, 5, theme.panel2);
            gfx_round_outline(cx + 6, ly + 1, kw, 20, 5, theme.border);
            gfx_text(cx + 12, ly + 3, line->key, theme.accent);
            gfx_text(cx + 6 + kw + 12, ly + 2, line->text, theme.text_dim);
            break;
        }

        case HELP_GAP:
        default:
            break;
        }
    }
    gfx_reset_clip();

    if (count > rows)
        ui_scrollbar(x + cw - 12, cy, 6, cch, scroll, count, rows);

    char left[160];
    ksnprintf(left, sizeof(left), "%s  |  %d lines  |  PgUp/PgDn scroll",
              help_chapters[h->chapter], count);
    app_status(w, left, "F1 help  |  Esc close", theme.text_faint);
}

/* â”€â”€ Input â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void help_mouse(window_t *w, int mx, int my, int button, int pressed)
{
    help_state_t *h = (help_state_t *)w->state;
    if (!h || button != MOUSE_BTN_LEFT || !pressed) return;

    int ax = mx + CX(w);
    int ay = my + CY(w);
    for (int i = 0; i < HELP_CHAPTERS; i++) {
        int bx, by, bw, bh;
        help_sidebar_rect(w, i, &bx, &by, &bw, &bh);
        if (app_hit(ax, ay, bx, by, bw, bh)) {
            h->chapter = i;
            return;
        }
    }
}

static void help_key(window_t *w, const key_event_t *ev)
{
    help_state_t *h = (help_state_t *)w->state;
    if (!h) return;

    int rows = help_visible_rows(w);
    int *scroll = &h->scroll[h->chapter];

    if (ev->ch >= '1' && ev->ch <= '5') {
        h->chapter = ev->ch - '1';
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
    case KEY_LEFT:
        if (h->chapter > 0) h->chapter--;
        return;
    case KEY_RIGHT:
        if (h->chapter + 1 < HELP_CHAPTERS) h->chapter++;
        return;
    default:
        break;
    }
}

static void help_wheel(window_t *w, int delta, int x, int y)
{
    help_state_t *h = (help_state_t *)w->state;
    UNUSED(x); UNUSED(y);
    if (!h) return;
    h->scroll[h->chapter] += delta > 0 ? -2 : 2;
    if (h->scroll[h->chapter] < 0) h->scroll[h->chapter] = 0;
}

static void help_close(window_t *w)
{
    if (w->state) {
        kfree(w->state);
        w->state = NULL;
    }
}

void app_help_open(void)
{
    window_t *w = wm_find(APP_HELP);
    if (w) {
        wm_focus(w);
        return;
    }

    help_state_t *h = (help_state_t *)kzalloc(sizeof(help_state_t));
    if (!h) {
        toast_show("Help", "Not enough memory", TOAST_ERROR);
        return;
    }

    int ww = min(900, max(650, gfx.width - 170));
    int wh = min(660, max(470, gfx.height - 160));
    int wx = max(24, (gfx.width - ww) / 2 + 8);
    int wy = max(50, (gfx.height - wh) / 2 - 6);

    window_t *win = wm_create(APP_HELP, "Help - NOVA Developer Guide", wx, wy, ww, wh,
                              620, 440, help_draw, help_key, help_mouse,
                              help_wheel, help_close, h);
    if (!win) kfree(h);
}
