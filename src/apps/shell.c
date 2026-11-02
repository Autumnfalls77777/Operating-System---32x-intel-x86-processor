/* ============================================================================
 * src/apps/shell.c  -  NOVA shell interpreter
 *
 * A small but practical POSIX-flavoured shell backed by the in-memory VFS.
 * It also exposes the kernel: processes, scheduler, heap, PCI, IDT, CPUID and
 * INT 0x80 syscalls are all reachable from the prompt.
 * ============================================================================ */

#include "kernel.h"
#include "shell.h"
#include "terminal.h"
#include "vfs.h"
#include "memory.h"
#include "timer.h"
#include "rtc.h"
#include "process.h"
#include "cpu.h"
#include "pci.h"
#include "interrupts.h"
#include "syscall.h"
#include "system.h"
#include "apps.h"
#include "settings.h"
#include "theme.h"
#include "gfx.h"

/* â”€â”€ Redirect support â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

#define SHELL_SCRIPT_MAX VFS_MAX_FILE_SIZE
#define SHELL_REDIR_MAX  VFS_MAX_FILE_SIZE

static char redir_buf[SHELL_REDIR_MAX];
static int  redir_len;
static int  redir_active;
static int  redir_append;
static char redir_path[VFS_PATH_MAX];

static char script_buf[SHELL_SCRIPT_MAX + 1];
static int  script_depth;

static void out(terminal_t *t, const char *text, u32 color)
{
    if (!t || !text) return;
    if (redir_active) {
        int n = kstrlen(text);
        if (redir_len + n >= SHELL_REDIR_MAX) n = SHELL_REDIR_MAX - 1 - redir_len;
        if (n > 0) {
            memcpy(redir_buf + redir_len, text, (size_t)n);
            redir_len += n;
        }
        return;
    }
    term_write(t, text, color);
}

static void out_len(terminal_t *t, const char *text, int len, u32 color)
{
    if (!t || !text || len <= 0) return;
    if (redir_active) {
        if (redir_len + len >= SHELL_REDIR_MAX) len = SHELL_REDIR_MAX - 1 - redir_len;
        if (len > 0) {
            memcpy(redir_buf + redir_len, text, (size_t)len);
            redir_len += len;
        }
        return;
    }
    term_write_len(t, text, len, color);
}

static void append_text(char *dst, int cap, const char *src)
{
    int len = kstrlen(dst);
    while (src && *src && len < cap - 1) dst[len++] = *src++;
    dst[len] = '\0';
}

static void outf(terminal_t *t, u32 color, const char *fmt, ...)
{
    char buf[1024];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    out(t, buf, color);
}

static void nl(terminal_t *t) { out(t, "\n", TERM_C_DEFAULT); }

static int is_space(char c)
{
    return c == ' ' || c == '\t';
}

static char *skip_spaces(char *p)
{
    while (p && *p && is_space(*p)) p++;
    return p;
}

static int next_token(char **cursor, char *out_buf, int cap)
{
    if (!cursor || !*cursor || !out_buf || cap <= 0) return 0;
    char *p = skip_spaces(*cursor);
    if (!*p) {
        *cursor = p;
        return 0;
    }

    int n = 0;
    if (*p == '"' || *p == '\'') {
        char quote = *p++;
        while (*p && *p != quote) {
            if (n < cap - 1) out_buf[n++] = *p;
            p++;
        }
        if (*p == quote) p++;
    } else {
        while (*p && !is_space(*p)) {
            if (n < cap - 1) out_buf[n++] = *p;
            p++;
        }
    }
    out_buf[n] = '\0';
    *cursor = p;
    return 1;
}

/* â”€â”€ Path helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static int resolve(terminal_t *t, const char *path, char *full, int cap)
{
    if (!path || !*path) {
        out(t, "nova: missing path\n", TERM_C_ERR);
        return -1;
    }
    int node = vfs_lookup(path, t->cwd);
    if (node < 0) {
        outf(t, TERM_C_ERR, "nova: %s: no such file or directory\n", path);
        return -1;
    }
    if (full && cap > 0) vfs_path(node, full, cap);
    return node;
}

static int cat_file(terminal_t *t, const char *path)
{
    u32 size = 0;
    char *data = vfs_data(path, t->cwd, &size);
    if (!data) {
        int node = vfs_lookup(path, t->cwd);
        if (node >= 0 && vfs_is_dir(node))
            outf(t, TERM_C_ERR, "nova: %s: is a directory\n", path);
        else
            outf(t, TERM_C_ERR, "nova: %s: cannot read\n", path);
        return -1;
    }
    out_len(t, data, (int)size, TERM_C_DEFAULT);
    if (size > 0 && data[size - 1] != '\n') nl(t);
    return 0;
}

static void rm_tree(terminal_t *t, int node, int depth)
{
    if (node < 0 || depth > 10) return;
    if (vfs_is_dir(node)) {
        int entries[VFS_MAX_NODES];
        int count = vfs_list(node, entries, VFS_MAX_NODES);
        for (int i = 0; i < count; i++) rm_tree(t, entries[i], depth + 1);
    }
    char path[VFS_PATH_MAX];
    vfs_path(node, path, sizeof(path));
    vfs_unlink(path, "/");
}

static void tree_dir(terminal_t *t, int node, const char *prefix,
                     int depth, int *printed)
{
    if (depth > 6 || *printed > 120) return;

    int entries[VFS_MAX_NODES];
    int count = vfs_list(node, entries, VFS_MAX_NODES);

    for (int i = 0; i < count && *printed < 120; i++) {
        int n = entries[i];
        int last = (i == count - 1);
        outf(t, TERM_C_DIM, "%s%s", prefix, last ? "`-- " : "|-- ");
        if (vfs_is_dir(n)) {
            outf(t, TERM_C_ACCENT, "%s/\n", vfs_name(n));
            (*printed)++;
            char next[160];
            ksnprintf(next, sizeof(next), "%s%s", prefix, last ? "    " : "|   ");
            tree_dir(t, n, next, depth + 1, printed);
        } else {
            outf(t, vfs_type_name(n)[0] == 'p' ? TERM_C_OK : TERM_C_DEFAULT,
                 "%s\n", vfs_name(n));
            (*printed)++;
        }
    }
}

/* â”€â”€ System information commands â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static const char *irq_names[16] = {
    "timer", "keyboard", "cascade", "com2", "com1", "lpt2", "floppy", "lpt1",
    "rtc", "acpi", "pci0", "pci1", "mouse", "fpu", "ide", "ide2"
};

static void cmd_neofetch(terminal_t *t)
{
    static const char *art[8] = {
        "        /\\        ",
        "       /  \\       ",
        "      / /\\ \\      ",
        "     / /  \\ \\     ",
        "    / /    \\ \\    ",
        "   /_/      \\_\\   ",
        "   \\_\\      /_/   ",
        "    \\_\\    /_/    "
    };

    cpu_info_t cpu;
    cpuid_info(&cpu);
    char brand[64];
    cpuid_format_brand(brand, sizeof(brand));

    mem_info_t mi;
    mem_info(&mi);

    for (int i = 0; i < 8; i++) {
        out(t, art[i], i < 4 ? theme.accent : theme.accent2);
        switch (i) {
        case 0: outf(t, TERM_C_DIM, "  dev@nova\n"); break;
        case 1: outf(t, TERM_C_DIM, "  ---------\n"); break;
        case 2: outf(t, TERM_C_DIM, "  OS      "); outf(t, TERM_C_DEFAULT, "Altitude OS " NOVA_VERSION " (" NOVA_CODENAME ")\n"); break;
        case 3: outf(t, TERM_C_DIM, "  Kernel  "); outf(t, TERM_C_DEFAULT, "%s\n", system_arch()); break;
        case 4: outf(t, TERM_C_DIM, "  CPU     "); outf(t, TERM_C_DEFAULT, "%s\n", brand); break;
        case 5: outf(t, TERM_C_DIM, "  RAM     "); outf(t, TERM_C_DEFAULT, "%u MiB usable\n", system_total_ram_kb() / 1024); break;
        case 6: outf(t, TERM_C_DIM, "  Heap    "); outf(t, TERM_C_DEFAULT, "%u / %u KiB used\n", mi.used / 1024, mi.total / 1024); break;
        case 7: outf(t, TERM_C_DIM, "  Display "); outf(t, TERM_C_DEFAULT, "%ux%u @ %ubpp\n",
                       g_boot_info ? g_boot_info->width : 0,
                       g_boot_info ? g_boot_info->height : 0,
                       g_boot_info ? g_boot_info->bpp : 0); break;
        default: nl(t); break;
        }
    }
    nl(t);
}

static void cmd_help(terminal_t *t)
{
    out(t, "Altitude OS shell commands\n", TERM_C_ACCENT);
    out(t, "  Files     ls [-l] cd pwd cat touch mkdir rm rmdir mv cp chmod\n", TERM_C_DEFAULT);
    out(t, "            tree find? grep hexdump wc echo clear\n", TERM_C_DEFAULT);
    out(t, "  System    uname uptime date free meminfo dmesg lspci cpuinfo idt\n", TERM_C_DEFAULT);
    out(t, "  Kernel    ps kill spawn sched tick syscalls\n", TERM_C_DEFAULT);
    out(t, "  Apps      open <app>  edit <file>  apps\n", TERM_C_DEFAULT);
    out(t, "  Power     reboot halt poweroff\n", TERM_C_DEFAULT);
    out(t, "  Scripts   sh <file>  echo text > file  echo text >> file\n", TERM_C_DEFAULT);
    out(t, "  Fun       neofetch about history beep sleep\n", TERM_C_DEFAULT);
    out(t, "Use 'help <command>' for a one-line hint.\n", TERM_C_DIM);
}

static void cmd_apps(terminal_t *t)
{
    out(t, "Applications\n", TERM_C_ACCENT);
    for (int i = 0; i < APP_COUNT; i++) {
        const app_info_t *a = app_info((app_id_t)i);
        outf(t, TERM_C_DEFAULT, "  %-12s %s\n", a->short_name, a->description);
    }
}

static void cmd_ls(terminal_t *t, char *args)
{
    int long_fmt = 0;
    char path[VFS_PATH_MAX];
    kstrcpy(path, t->cwd);

    char tok[VFS_PATH_MAX];
    while (next_token(&args, tok, sizeof(tok))) {
        if (tok[0] == '-' && tok[1]) {
            for (int i = 1; tok[i]; i++) {
                if (tok[i] == 'l') long_fmt = 1;
            }
        } else {
            kstrncpy(path, tok, sizeof(path));
        }
    }

    char full[VFS_PATH_MAX];
    int node = resolve(t, path, full, sizeof(full));
    if (node < 0) return;
    if (!vfs_is_dir(node)) {
        outf(t, TERM_C_ERR, "nova: %s: not a directory\n", path);
        return;
    }

    int entries[VFS_MAX_NODES];
    int count = vfs_list(node, entries, VFS_MAX_NODES);

    if (long_fmt) {
        out(t, "MODE       SIZE      NAME\n", TERM_C_DIM);
        out(t, "---------- ----      ----\n", TERM_C_DIM);
        for (int i = 0; i < count; i++) {
            char mode[16], size[24];
            vfs_format_mode(vfs_mode(entries[i]), mode);
            vfs_format_size(vfs_size(entries[i]), size, sizeof(size));
            outf(t, vfs_is_dir(entries[i]) ? TERM_C_ACCENT : TERM_C_DEFAULT,
                 "%s %-9s %s%s\n", mode, size, vfs_name(entries[i]),
                 vfs_is_dir(entries[i]) ? "/" : "");
        }
        outf(t, TERM_C_DIM, "%d entries\n", count);
        return;
    }

    int col = 0;
    for (int i = 0; i < count; i++) {
        char name[48];
        ksnprintf(name, sizeof(name), "%s%s", vfs_name(entries[i]),
                  vfs_is_dir(entries[i]) ? "/" : "");
        int width = 24;
        if (kstrlen(name) >= width) width = kstrlen(name) + 2;
        out(t, name, vfs_is_dir(entries[i]) ? TERM_C_ACCENT : TERM_C_DEFAULT);
        for (int s = kstrlen(name); s < width; s++) out(t, " ", TERM_C_DEFAULT);
        if (++col >= 3) { col = 0; nl(t); }
    }
    if (col) nl(t);
    outf(t, TERM_C_DIM, "%d entries\n", count);
}

static void cmd_hexdump(terminal_t *t, const char *path)
{
    u32 size = 0;
    unsigned char *data = (unsigned char *)vfs_data(path, t->cwd, &size);
    if (!data) {
        outf(t, TERM_C_ERR, "hexdump: %s: cannot read\n", path);
        return;
    }
    for (u32 off = 0; off < size; off += 16) {
        outf(t, TERM_C_DIM, "%08x  ", off);
        for (int i = 0; i < 16; i++) {
            if (off + i < size) outf(t, TERM_C_DEFAULT, "%02x ", data[off + i]);
            else out(t, "   ", TERM_C_DEFAULT);
            if (i == 7) out(t, " ", TERM_C_DEFAULT);
        }
        out(t, " |", TERM_C_DIM);
        for (int i = 0; i < 16 && off + i < size; i++) {
            unsigned char c = data[off + i];
            char s[2] = { (char)((c >= 32 && c <= 126) ? c : '.'), '\0' };
            out(t, s, TERM_C_DEFAULT);
        }
        out(t, "|\n", TERM_C_DIM);
    }
    outf(t, TERM_C_DIM, "%u bytes\n", size);
}

static void cmd_grep(terminal_t *t, const char *pattern, const char *path)
{
    u32 size = 0;
    char *data = vfs_data(path, t->cwd, &size);
    if (!data) {
        outf(t, TERM_C_ERR, "grep: %s: cannot read\n", path);
        return;
    }

    int line_no = 1;
    char *p = data;
    char *end = data + size;
    while (p < end) {
        char *eol = p;
        while (eol < end && *eol != '\n') eol++;
        int len = (int)(eol - p);
        char line[512];
        int n = min(len, (int)sizeof(line) - 1);
        memcpy(line, p, (size_t)n);
        line[n] = '\0';
        if (kstrstr(line, pattern)) {
            outf(t, TERM_C_DIM, "%d:", line_no);
            out(t, line, TERM_C_DEFAULT);
            nl(t);
        }
        line_no++;
        p = (eol < end) ? eol + 1 : end;
    }
}

static void cmd_wc(terminal_t *t, const char *path)
{
    u32 size = 0;
    char *data = vfs_data(path, t->cwd, &size);
    if (!data) {
        outf(t, TERM_C_ERR, "wc: %s: cannot read\n", path);
        return;
    }
    u32 lines = 0, words = 0;
    int in_word = 0;
    for (u32 i = 0; i < size; i++) {
        if (data[i] == '\n') lines++;
        if (data[i] == ' ' || data[i] == '\t' || data[i] == '\n' || data[i] == '\r') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }
    outf(t, TERM_C_DEFAULT, "%u lines  %u words  %u bytes  %s\n",
         lines, words, size, path);
}

static void cmd_ps(terminal_t *t)
{
    pcb_t *table = proc_table();
    out(t, " PID  NAME              STATE     PRI  BURST  LEFT  CPU\n", TERM_C_DIM);
    for (int i = 0; i < PROC_TABLE_SIZE; i++) {
        pcb_t *p = &table[i];
        if (p->state == STATE_UNUSED) continue;
        u32 color = TERM_C_DEFAULT;
        if (p->state == STATE_RUNNING) color = TERM_C_OK;
        else if (p->state == STATE_READY) color = TERM_C_ACCENT;
        else if (p->state == STATE_TERMINATED) color = TERM_C_DIM;
        outf(t, color, " %3d  %-16s  %-8s  %2d   %4d  %4d  %4u\n",
             p->pid, p->name, proc_state_name(p->state), p->priority,
             p->burst_total, p->burst_left, p->cpu_ticks);
    }
    outf(t, TERM_C_DIM, "active %d, spawned %d, completed %d\n",
         proc_active_count(), proc_total_spawned(), proc_completed_count());
}

static void cmd_sched(terminal_t *t, char *args)
{
    char arg[32];
    if (next_token(&args, arg, sizeof(arg))) {
        sched_algo_t algo = sched_get_algo();
        if (kstrcasecmp(arg, "fcfs") == 0) algo = SCHED_FCFS;
        else if (kstrcasecmp(arg, "rr") == 0) algo = SCHED_RR;
        else if (kstrcasecmp(arg, "prio") == 0 || kstrcasecmp(arg, "priority") == 0) algo = SCHED_PRIORITY;
        else if (kstrcasecmp(arg, "sjf") == 0) algo = SCHED_SJF;
        else {
            outf(t, TERM_C_ERR, "sched: unknown policy '%s'\n", arg);
            return;
        }
        sched_set_algo(algo);
    }

    sched_stats_t st;
    sched_stats(&st);
    outf(t, TERM_C_ACCENT, "policy: %s\n", sched_algo_name());
    outf(t, TERM_C_DEFAULT, "ticks %u  switches %u  completed %u  queue %d\n",
         st.ticks, st.switches, st.completed, st.queue_len);
    if (st.current)
        outf(t, TERM_C_OK, "running: pid %d  %s\n", st.current->pid, st.current->name);
    else
        out(t, "running: <idle>\n", TERM_C_DIM);
    for (int i = 0; i < sched_queue_len(); i++) {
        pcb_t *p = sched_queue_at(i);
        outf(t, TERM_C_DIM, "  [%d] pid %d %s  prio %d  left %d\n",
             i, p->pid, p->name, p->priority, p->burst_left);
    }
}

static void cmd_syscalls(terminal_t *t)
{
    out(t, "INT 0x80  eax = call number, ebx/ecx/edx/esi/edi = args\n", TERM_C_ACCENT);
    out(t, "NUM  NAME        ARGS  DESCRIPTION\n", TERM_C_DIM);
    for (int i = 1; i < SYS_COUNT; i++) {
        const syscall_info_t *s = syscall_info(i);
        if (!s) continue;
        outf(t, TERM_C_DEFAULT, "%3d  %-10s  %d     %s\n", i, s->name, s->args, s->description);
    }
    outf(t, TERM_C_DIM, "calls since boot: %u\n", syscall_count());
}

static void cmd_dmesg(terminal_t *t)
{
    for (int i = 0; i < klog_count(); i++) {
        int level = 0;
        const char *line = klog_line(i, &level);
        u32 color = TERM_C_DIM;
        if (level == KLOG_LEVEL_OK) color = TERM_C_OK;
        else if (level == KLOG_LEVEL_WARN) color = TERM_C_WARN;
        else if (level == KLOG_LEVEL_ERR) color = TERM_C_ERR;
        else if (level == KLOG_LEVEL_BOOT) color = TERM_C_ACCENT;
        outf(t, color, "%s\n", line ? line : "");
    }
}

static void cmd_lspci(terminal_t *t)
{
    pci_device_t devs[PCI_MAX_DEVICES];
    int count = pci_scan(devs, PCI_MAX_DEVICES);
    out(t, "BUS:SLOT.FN  VENDOR:DEVICE  CLASS  DESCRIPTION\n", TERM_C_DIM);
    for (int i = 0; i < count; i++) {
        outf(t, TERM_C_DEFAULT, "%02x:%02x.%x      %04x:%04x      %02x%02x   %s\n",
             devs[i].bus, devs[i].slot, devs[i].func,
             devs[i].vendor, devs[i].device,
             devs[i].class_code, devs[i].subclass,
             pci_class_name(devs[i].class_code, devs[i].subclass));
    }
    outf(t, TERM_C_DIM, "%d device(s)\n", count);
}

static void cmd_cpuinfo(terminal_t *t)
{
    cpu_info_t cpu;
    cpuid_info(&cpu);
    char features[512];
    cpuid_format_features(features, sizeof(features));

    outf(t, TERM_C_ACCENT, "vendor    %s\n", cpu.vendor);
    outf(t, TERM_C_DEFAULT, "brand     %s\n", cpu.brand);
    outf(t, TERM_C_DEFAULT, "family    %u   model %u   stepping %u\n",
         cpu.family, cpu.model, cpu.stepping);
    outf(t, TERM_C_DEFAULT, "leaves    basic 0x%x  extended 0x%x\n",
         cpu.max_leaf, cpu.max_ext_leaf);
    outf(t, TERM_C_DIM, "flags     %s\n", features);
}

static void cmd_idt(terminal_t *t)
{
    outf(t, TERM_C_ACCENT, "IDT base 0x%08x  limit 0x%x  interrupts %u\n",
         idt_base(), idt_limit(), interrupt_total_count());
    out(t, "VEC  OFFSET      FLAGS  IRQ\n", TERM_C_DIM);
    for (int v = 0; v < 48; v++) {
        u32 color = TERM_C_DEFAULT;
        if (v == 32 || v == 33 || v == 44 || v == 128) color = TERM_C_OK;
        outf(t, color, "%3d  0x%08x  0x%02x   %s\n",
             v, idt_gate_offset(v), idt_gate_flags(v),
             (v >= 32 && v < 48) ? irq_names[v - 32] : "-");
    }
    outf(t, TERM_C_DIM, "vector 128 (syscall) offset 0x%08x flags 0x%02x\n",
         idt_gate_offset(128), idt_gate_flags(128));
}

static void cmd_free(terminal_t *t)
{
    mem_info_t mi;
    mem_info(&mi);
    outf(t, TERM_C_ACCENT, "heap        %u KiB total\n", mi.total / 1024);
    outf(t, TERM_C_DEFAULT, "used        %u KiB\n", mi.used / 1024);
    outf(t, TERM_C_DEFAULT, "free        %u KiB\n", mi.free / 1024);
    outf(t, TERM_C_DEFAULT, "blocks      %u   largest free %u KiB\n",
         mi.blocks, mi.largest_free / 1024);
    outf(t, TERM_C_DEFAULT, "RAM         %u KiB usable\n", system_total_ram_kb());
    outf(t, TERM_C_DEFAULT, "VFS         %d nodes, %u bytes\n", vfs_used_nodes(), vfs_used_bytes());
}

static void cmd_uptime(terminal_t *t)
{
    u32 secs = timer_seconds();
    u32 days = secs / 86400u;
    u32 hours = (secs / 3600u) % 24u;
    u32 mins = (secs / 60u) % 60u;
    u32 s = secs % 60u;
    if (days)
        outf(t, TERM_C_DEFAULT, "up %ud %02u:%02u:%02u  |  %u ticks  |  %u interrupts\n",
             days, hours, mins, s, timer_ticks(), interrupt_total_count());
    else
        outf(t, TERM_C_DEFAULT, "up %02u:%02u:%02u  |  %u ticks  |  %u interrupts\n",
             hours, mins, s, timer_ticks(), interrupt_total_count());
}

static void cmd_date(terminal_t *t)
{
    rtc_time_t now;
    rtc_read(&now);
    char date[64], time[32];
    rtc_format_date(&now, date, sizeof(date));
    rtc_format_time_sec(&now, time, sizeof(time));
    outf(t, TERM_C_DEFAULT, "%s  %s\n", date, time);
}

static void cmd_about(terminal_t *t)
{
    out(t, "Altitude OS " NOVA_VERSION " \"" NOVA_CODENAME "\" (" NOVA_BUILD ")\n", TERM_C_ACCENT);
    out(t, "A programmer-first 32-bit operating system for x86.\n", TERM_C_DEFAULT);
    out(t, "Bootloader, kernel, compositor, filesystem and applications are\n", TERM_C_DEFAULT);
    out(t, "all part of this image - nothing is hidden behind a black box.\n", TERM_C_DEFAULT);
    out(t, "Try: neofetch, devtools, open devtools, cat /proc/cpuinfo\n", TERM_C_DIM);
}

static void cmd_open(terminal_t *t, char *args)
{
    char name[64];
    if (!next_token(&args, name, sizeof(name))) {
        cmd_apps(t);
        return;
    }
    if (app_id_from_name(name) < 0) {
        outf(t, TERM_C_ERR, "open: unknown application '%s'\n", name);
        cmd_apps(t);
        return;
    }
    app_open_named(name);
    outf(t, TERM_C_OK, "launched %s\n", name);
}

static void cmd_edit(terminal_t *t, char *args)
{
    char path[VFS_PATH_MAX];
    if (!next_token(&args, path, sizeof(path))) {
        out(t, "usage: edit <file>\n", TERM_C_ERR);
        return;
    }
    int node = vfs_lookup(path, t->cwd);
    if (node < 0) {
        outf(t, TERM_C_ERR, "edit: %s: no such file\n", path);
        return;
    }
    if (vfs_is_dir(node)) {
        outf(t, TERM_C_ERR, "edit: %s: is a directory\n", path);
        return;
    }
    char full[VFS_PATH_MAX];
    vfs_path(node, full, sizeof(full));
    app_editor_open_file(full);
    outf(t, TERM_C_OK, "editing %s\n", full);
}

static void cmd_sh(terminal_t *t, char *args)
{
    char path[VFS_PATH_MAX];
    if (!next_token(&args, path, sizeof(path))) {
        out(t, "usage: sh <script>\n", TERM_C_ERR);
        return;
    }

    if (script_depth >= 6) {
        out(t, "sh: script nesting too deep\n", TERM_C_ERR);
        return;
    }

    int n = vfs_read_file(path, t->cwd, script_buf, sizeof(script_buf));
    if (n < 0) {
        outf(t, TERM_C_ERR, "sh: %s: cannot read\n", path);
        return;
    }

    script_depth++;
    char *line = script_buf;
    while (*line) {
        char *eol = kstrchr(line, '\n');
        if (eol) *eol = '\0';
        char *work = line;
        while (*work == ' ' || *work == '\t') work++;
        if (*work && *work != '#') {
            outf(t, TERM_C_DIM, "sh> %s\n", work);
            /* Reuse the main dispatch without recursion through shell_execute. */
            extern void shell_execute_line(terminal_t *, char *);
            shell_execute_line(t, work);
        }
        if (!eol) break;
        line = eol + 1;
    }
    script_depth--;
}

static void cmd_history(terminal_t *t)
{
    if (t->history_count == 0) {
        out(t, "history is empty\n", TERM_C_DIM);
        return;
    }
    for (int i = 0; i < t->history_count; i++)
        outf(t, TERM_C_DIM, "%3d  %s\n", i + 1, t->history[i % TERM_HISTORY]);
}

/* â”€â”€ Redirect parsing â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static int parse_redirect(char *line, int *append, char *path, int cap)
{
    int in_single = 0, in_double = 0;
    for (char *p = line; *p; p++) {
        if (*p == '\'' && !in_double) in_single = !in_single;
        else if (*p == '"' && !in_single) in_double = !in_double;
        else if (*p == '>' && !in_single && !in_double) {
            int app = 0;
            char *r = p + 1;
            if (*r == '>') { app = 1; r++; }
            r = skip_spaces(r);
            int n = 0;
            while (*r && n < cap - 1) path[n++] = *r++;
            path[n] = '\0';
            kstrtrim(path);
            int plen = kstrlen(path);
            if (plen >= 2 && ((path[0] == '"' && path[plen - 1] == '"') ||
                              (path[0] == '\'' && path[plen - 1] == '\''))) {
                memmove(path, path + 1, (size_t)(plen - 2));
                path[plen - 2] = '\0';
            }
            char *end = p;
            while (end > line && (end[-1] == ' ' || end[-1] == '\t')) end--;
            *end = '\0';
            *append = app;
            return path[0] ? 1 : 0;
        }
    }
    return 0;
}

/* â”€â”€ Command dispatch â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static int run_command(terminal_t *t, char *line)
{
    char name[64];
    char *args = line;
    if (!next_token(&args, name, sizeof(name))) return 1;

    if (kstrcmp(name, "help") == 0) { cmd_help(t); return 1; }
    if (kstrcmp(name, "clear") == 0) { term_clear(t); return 1; }
    if (kstrcmp(name, "echo") == 0) {
        int no_nl = 0;
        char tok[512];
        char joined[1024];
        joined[0] = '\0';
        int first = 1;
        while (next_token(&args, tok, sizeof(tok))) {
            if (first && kstrcmp(tok, "-n") == 0) { no_nl = 1; first = 0; continue; }
            if (!first) append_text(joined, sizeof(joined), " ");
            append_text(joined, sizeof(joined), tok);
            first = 0;
        }
        out(t, joined, TERM_C_DEFAULT);
        if (!no_nl) nl(t);
        return 1;
    }
    if (kstrcmp(name, "pwd") == 0) { outf(t, TERM_C_DEFAULT, "%s\n", t->cwd); return 1; }
    if (kstrcmp(name, "cd") == 0) {
        char path[VFS_PATH_MAX];
        if (!next_token(&args, path, sizeof(path))) kstrcpy(path, "/home/dev");
        int node = resolve(t, path, NULL, 0);
        if (node < 0) return 1;
        if (!vfs_is_dir(node)) { outf(t, TERM_C_ERR, "cd: %s: not a directory\n", path); return 1; }
        vfs_path(node, t->cwd, sizeof(t->cwd));
        return 1;
    }
    if (kstrcmp(name, "ls") == 0) { cmd_ls(t, args); return 1; }
    if (kstrcmp(name, "cat") == 0) {
        char path[VFS_PATH_MAX];
        int any = 0;
        while (next_token(&args, path, sizeof(path))) { cat_file(t, path); any = 1; }
        if (!any) out(t, "usage: cat <file>...\n", TERM_C_ERR);
        return 1;
    }
    if (kstrcmp(name, "touch") == 0) {
        char path[VFS_PATH_MAX];
        if (!next_token(&args, path, sizeof(path))) { out(t, "usage: touch <file>\n", TERM_C_ERR); return 1; }
        if (vfs_create(path, t->cwd) < 0) outf(t, TERM_C_ERR, "touch: %s: cannot create\n", path);
        return 1;
    }
    if (kstrcmp(name, "mkdir") == 0) {
        char path[VFS_PATH_MAX];
        if (!next_token(&args, path, sizeof(path))) { out(t, "usage: mkdir <dir>\n", TERM_C_ERR); return 1; }
        if (vfs_mkdir(path, t->cwd) < 0) outf(t, TERM_C_ERR, "mkdir: %s: cannot create\n", path);
        return 1;
    }
    if (kstrcmp(name, "rm") == 0 || kstrcmp(name, "rmdir") == 0) {
        int recursive = (kstrcmp(name, "rmdir") == 0);
        char tok[VFS_PATH_MAX];
        char targets[8][VFS_PATH_MAX];
        int target_count = 0;
        while (next_token(&args, tok, sizeof(tok))) {
            if (tok[0] == '-') {
                if (kstrchr(tok, 'r')) recursive = 1;
                continue;
            }
            if (target_count < 8) kstrncpy(targets[target_count++], tok, VFS_PATH_MAX);
        }
        if (target_count == 0) { outf(t, TERM_C_ERR, "usage: %s [-r] <path>\n", name); return 1; }
        for (int i = 0; i < target_count; i++) {
            int node = vfs_lookup(targets[i], t->cwd);
            if (node < 0) { outf(t, TERM_C_ERR, "%s: %s: not found\n", name, targets[i]); continue; }
            if (vfs_is_dir(node) && !recursive) {
                outf(t, TERM_C_ERR, "%s: %s: is a directory (use -r)\n", name, targets[i]);
                continue;
            }
            if (vfs_is_dir(node)) rm_tree(t, node, 0);
            else {
                char full[VFS_PATH_MAX];
                vfs_path(node, full, sizeof(full));
                vfs_unlink(full, "/");
            }
        }
        return 1;
    }
    if (kstrcmp(name, "mv") == 0) {
        char a[VFS_PATH_MAX], b[VFS_PATH_MAX];
        if (!next_token(&args, a, sizeof(a)) || !next_token(&args, b, sizeof(b))) {
            out(t, "usage: mv <src> <dst>\n", TERM_C_ERR);
            return 1;
        }
        if (vfs_rename(a, b, t->cwd) < 0) outf(t, TERM_C_ERR, "mv: %s -> %s failed\n", a, b);
        return 1;
    }
    if (kstrcmp(name, "cp") == 0) {
        char a[VFS_PATH_MAX], b[VFS_PATH_MAX];
        if (!next_token(&args, a, sizeof(a)) || !next_token(&args, b, sizeof(b))) {
            out(t, "usage: cp <src> <dst>\n", TERM_C_ERR);
            return 1;
        }
        if (vfs_copy(a, b, t->cwd) < 0) outf(t, TERM_C_ERR, "cp: %s -> %s failed\n", a, b);
        return 1;
    }
    if (kstrcmp(name, "chmod") == 0) {
        char mode[16], path[VFS_PATH_MAX];
        if (!next_token(&args, mode, sizeof(mode)) || !next_token(&args, path, sizeof(path))) {
            out(t, "usage: chmod <octal> <path>\n", TERM_C_ERR);
            return 1;
        }
        u32 m = kstrtoul(mode, NULL, 8);
        int node = vfs_lookup(path, t->cwd);
        if (node < 0 || vfs_chmod(path, t->cwd, (u16)m) < 0)
            outf(t, TERM_C_ERR, "chmod: %s: failed\n", path);
        return 1;
    }
    if (kstrcmp(name, "tree") == 0) {
        char path[VFS_PATH_MAX];
        kstrcpy(path, t->cwd);
        next_token(&args, path, sizeof(path));
        int node = resolve(t, path, NULL, 0);
        if (node < 0) return 1;
        if (!vfs_is_dir(node)) { outf(t, TERM_C_ERR, "tree: %s: not a directory\n", path); return 1; }
        int printed = 0;
        outf(t, TERM_C_ACCENT, "%s\n", path);
        tree_dir(t, node, "", 0, &printed);
        outf(t, TERM_C_DIM, "%d entries\n", printed);
        return 1;
    }
    if (kstrcmp(name, "grep") == 0) {
        char pat[128], path[VFS_PATH_MAX];
        if (!next_token(&args, pat, sizeof(pat)) || !next_token(&args, path, sizeof(path))) {
            out(t, "usage: grep <pattern> <file>\n", TERM_C_ERR);
            return 1;
        }
        cmd_grep(t, pat, path);
        return 1;
    }
    if (kstrcmp(name, "hexdump") == 0 || kstrcmp(name, "hex") == 0) {
        char path[VFS_PATH_MAX];
        if (!next_token(&args, path, sizeof(path))) { out(t, "usage: hexdump <file>\n", TERM_C_ERR); return 1; }
        cmd_hexdump(t, path);
        return 1;
    }
    if (kstrcmp(name, "wc") == 0) {
        char path[VFS_PATH_MAX];
        if (!next_token(&args, path, sizeof(path))) { out(t, "usage: wc <file>\n", TERM_C_ERR); return 1; }
        cmd_wc(t, path);
        return 1;
    }
    if (kstrcmp(name, "uname") == 0) {
        outf(t, TERM_C_DEFAULT, "Altitude OS %s %s %s\n", NOVA_VERSION, NOVA_CODENAME, system_arch());
        return 1;
    }
    if (kstrcmp(name, "uptime") == 0) { cmd_uptime(t); return 1; }
    if (kstrcmp(name, "date") == 0) { cmd_date(t); return 1; }
    if (kstrcmp(name, "free") == 0) { cmd_free(t); return 1; }
    if (kstrcmp(name, "meminfo") == 0) { cat_file(t, "/proc/meminfo"); return 1; }
    if (kstrcmp(name, "ps") == 0) { cmd_ps(t); return 1; }
    if (kstrcmp(name, "kill") == 0) {
        char tok[32];
        if (!next_token(&args, tok, sizeof(tok))) { out(t, "usage: kill <pid>\n", TERM_C_ERR); return 1; }
        int pid = katoi(tok);
        if (proc_kill(pid) < 0) outf(t, TERM_C_ERR, "kill: %d: no such process\n", pid);
        else outf(t, TERM_C_OK, "terminated pid %d\n", pid);
        return 1;
    }
    if (kstrcmp(name, "spawn") == 0) {
        char pname[PROC_NAME_LEN];
        char tok[32];
        int prio = 5, burst = 14;
        if (!next_token(&args, pname, sizeof(pname))) kstrcpy(pname, "worker");
        if (next_token(&args, tok, sizeof(tok))) prio = katoi(tok);
        if (next_token(&args, tok, sizeof(tok))) burst = katoi(tok);
        pcb_t *p = proc_spawn(pname, prio, burst);
        if (p) outf(t, TERM_C_OK, "spawned pid %d (%s) prio %d burst %d\n",
                    p->pid, p->name, p->priority, p->burst_total);
        else out(t, "spawn failed: process table full\n", TERM_C_ERR);
        return 1;
    }
    if (kstrcmp(name, "sched") == 0) { cmd_sched(t, args); return 1; }
    if (kstrcmp(name, "tick") == 0) {
        char tok[32];
        int n = 1;
        if (next_token(&args, tok, sizeof(tok))) n = clamp(katoi(tok), 1, 2000);
        int ran = 0;
        pcb_t *last = NULL;
        for (int i = 0; i < n; i++) { last = sched_tick(); if (last) ran++; }
        outf(t, TERM_C_DEFAULT, "%d tick(s), %d process slice(s)", n, ran);
        if (last) outf(t, TERM_C_DIM, ", last pid %d", last->pid);
        nl(t);
        return 1;
    }
    if (kstrcmp(name, "syscalls") == 0 || kstrcmp(name, "syscall") == 0) { cmd_syscalls(t); return 1; }
    if (kstrcmp(name, "dmesg") == 0) { cmd_dmesg(t); return 1; }
    if (kstrcmp(name, "lspci") == 0) { cmd_lspci(t); return 1; }
    if (kstrcmp(name, "cpuinfo") == 0) { cmd_cpuinfo(t); return 1; }
    if (kstrcmp(name, "idt") == 0) { cmd_idt(t); return 1; }
    if (kstrcmp(name, "neofetch") == 0) { cmd_neofetch(t); return 1; }
    if (kstrcmp(name, "about") == 0) { cmd_about(t); return 1; }
    if (kstrcmp(name, "apps") == 0) { cmd_apps(t); return 1; }
    if (kstrcmp(name, "open") == 0 || kstrcmp(name, "app") == 0) { cmd_open(t, args); return 1; }
    if (kstrcmp(name, "edit") == 0) { cmd_edit(t, args); return 1; }
    if (kstrcmp(name, "sh") == 0 || kstrcmp(name, "source") == 0) { cmd_sh(t, args); return 1; }
    if (kstrcmp(name, "history") == 0) { cmd_history(t); return 1; }
    if (kstrcmp(name, "whoami") == 0) { out(t, "dev\n", TERM_C_DEFAULT); return 1; }
    if (kstrcmp(name, "hostname") == 0) { out(t, "nova\n", TERM_C_DEFAULT); return 1; }
    if (kstrcmp(name, "id") == 0) { out(t, "uid=1000(dev) gid=1000(dev) groups=1000(dev),10(kernel)\n", TERM_C_DEFAULT); return 1; }
    if (kstrcmp(name, "beep") == 0) {
        char hz[16], ms[16];
        int f = 880, d = 120;
        if (next_token(&args, hz, sizeof(hz))) f = katoi(hz);
        if (next_token(&args, ms, sizeof(ms))) d = katoi(ms);
        sys_call(SYS_BEEP, (u32)f, (u32)d, 0, 0, 0);
        return 1;
    }
    if (kstrcmp(name, "sleep") == 0) {
        char ms[16];
        int d = 500;
        if (next_token(&args, ms, sizeof(ms))) d = katoi(ms);
        timer_sleep_ms((u32)clamp(d, 0, 10000));
        return 1;
    }
    if (kstrcmp(name, "reboot") == 0) { app_power_reboot(); return 1; }
    if (kstrcmp(name, "halt") == 0 || kstrcmp(name, "poweroff") == 0 ||
        kstrcmp(name, "shutdown") == 0) { app_power_halt(); return 1; }
    if (kstrcmp(name, "exit") == 0 || kstrcmp(name, "logout") == 0) {
        out(t, "logout\n", TERM_C_WARN);
        t->input_len = -1;
        return 1;
    }

    outf(t, TERM_C_ERR, "nova: %s: command not found\n", name);
    out(t, "Type 'help' for the command list.\n", TERM_C_DIM);
    return 1;
}

/* â”€â”€ Line execution / redirection wrapper â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

void shell_execute_line(terminal_t *t, char *line)
{
    if (!t || !line) return;
    kstrtrim(line);
    if (!*line || *line == '#') return;

    int append = 0;
    char path[VFS_PATH_MAX];
    int redir = parse_redirect(line, &append, path, sizeof(path));
    if (redir) {
        redir_active = 1;
        redir_append = append;
        redir_len = 0;
        kstrncpy(redir_path, path, sizeof(redir_path));
    }

    run_command(t, line);

    if (redir) {
        int n = vfs_write_file(redir_path, t->cwd, redir_buf, (u32)redir_len, redir_append);
        redir_active = 0;
        if (n >= 0)
            outf(t, TERM_C_DIM, "(%d bytes %s %s)\n", redir_len,
                 redir_append ? "appended to" : "written to", redir_path);
        else
            outf(t, TERM_C_ERR, "redirect: %s: write failed\n", redir_path);
    }
}

void shell_execute(terminal_t *t, const char *line)
{
    if (!t || !line) return;

    char work[VFS_PATH_MAX + TERM_INPUT_MAX + 32];
    kstrncpy(work, line, sizeof(work));
    shell_execute_line(t, work);
}

void shell_print_prompt(terminal_t *t)
{
    if (!t) return;
    char prompt[VFS_PATH_MAX + 24];
    ksnprintf(prompt, sizeof(prompt), "nova@kernel:%s$ ", t->cwd);
    term_write(t, prompt, TERM_C_ACCENT);
}

void shell_welcome(terminal_t *t)
{
    if (!t) return;

    term_write(t, "  Altitude OS  " NOVA_VERSION "  \"" NOVA_CODENAME "\"\n", TERM_C_ACCENT);
    term_write(t, "  programmer operating system - i386 protected mode\n", TERM_C_DIM);
    nl(t);
    term_write(t, "  kernel symbols are visible; every subsystem has a shell command.\n", TERM_C_DEFAULT);
    term_write(t, "  type 'help', 'neofetch', 'ps', 'meminfo' or 'open devtools'.\n", TERM_C_DEFAULT);
    nl(t);

    char motd[512];
    int n = vfs_read_file("/etc/motd", "/", motd, sizeof(motd));
    if (n > 0) {
        term_write(t, motd, TERM_C_DIM);
        if (motd[n - 1] != '\n') nl(t);
        nl(t);
    }
}
