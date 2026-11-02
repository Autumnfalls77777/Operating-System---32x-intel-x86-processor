/* ============================================================================
 * src/fs/procfs.c  -  Generated /proc virtual files
 * ============================================================================ */

#include "kernel.h"
#include "procfs.h"
#include "system.h"
#include "memory.h"
#include "timer.h"
#include "interrupts.h"
#include "cpu.h"
#include "pci.h"
#include "syscall.h"
#include "process.h"
#include "vfs.h"

const char *procfs_files[] = {
    "version", "cpuinfo", "meminfo", "uptime", "interrupts",
    "pci", "e820", "syscalls", "sched", "ps", "mounts", "cmdline"
};
const int procfs_file_count = (int)(sizeof(procfs_files) / sizeof(procfs_files[0]));

static int add(char *buf, int cap, int *len, const char *fmt, ...)
{
    if (*len >= cap - 1) return 0;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = kvsnprintf(buf + *len, cap - *len, fmt, ap);
    __builtin_va_end(ap);
    if (n > 0) *len += min(n, cap - *len - 1);
    return n;
}

static int generate_version(char *buf, int cap)
{
    int n = 0;
    add(buf, cap, &n, "Altitude OS version %s (%s)\n", NOVA_VERSION, NOVA_CODENAME);
    add(buf, cap, &n, "Built %s\n", NOVA_BUILD);
    add(buf, cap, &n, "Architecture: %s\n", system_arch());
    add(buf, cap, &n, "Kernel base: 0x%x\n", KERNEL_BASE);
    add(buf, cap, &n, "Boot display: %ux%u %ubpp\n",
        g_boot_info ? g_boot_info->width : 0,
        g_boot_info ? g_boot_info->height : 0,
        g_boot_info ? g_boot_info->bpp : 0);
    return n;
}

static int generate_cpuinfo(char *buf, int cap)
{
    int n = 0;
    cpu_info_t cpu;
    cpuid_info(&cpu);
    char features[512];
    cpuid_format_features(features, sizeof(features));

    add(buf, cap, &n, "processor\t: 0\n");
    add(buf, cap, &n, "vendor_id\t: %s\n", cpu.vendor);
    add(buf, cap, &n, "model name\t: %s\n", cpu.brand);
    add(buf, cap, &n, "family\t\t: %u\n", cpu.family);
    add(buf, cap, &n, "model\t\t: %u\n", cpu.model);
    add(buf, cap, &n, "stepping\t: %u\n", cpu.stepping);
    add(buf, cap, &n, "max_leaf\t: 0x%x\n", cpu.max_leaf);
    add(buf, cap, &n, "max_ext_leaf\t: 0x%x\n", cpu.max_ext_leaf);
    add(buf, cap, &n, "flags\t\t: %s\n", features);
    add(buf, cap, &n, "\n");
    return n;
}

static int generate_meminfo(char *buf, int cap)
{
    int n = 0;
    mem_info_t info;
    mem_info(&info);

    add(buf, cap, &n, "MemTotal:      %u kB\n", system_total_ram_kb());
    add(buf, cap, &n, "HeapStart:     0x%08x\n", info.start);
    add(buf, cap, &n, "HeapTotal:     %u bytes (%u kB)\n", info.total, info.total / 1024);
    add(buf, cap, &n, "HeapUsed:      %u bytes\n", info.used);
    add(buf, cap, &n, "HeapFree:      %u bytes\n", info.free);
    add(buf, cap, &n, "HeapBlocks:    %u\n", info.blocks);
    add(buf, cap, &n, "LargestFree:   %u bytes\n", info.largest_free);
    add(buf, cap, &n, "VFSBytes:      %u\n", vfs_used_bytes());
    add(buf, cap, &n, "\nE820 map:\n");
    if (g_boot_info) {
        for (u32 i = 0; i < g_boot_info->e820_count && i < 16; i++) {
            e820_entry_t *e = &g_boot_info->e820[i];
            const char *type = e->type == 1 ? "usable" :
                               e->type == 2 ? "reserved" :
                               e->type == 3 ? "ACPI reclaim" :
                               e->type == 4 ? "ACPI NVS" : "unknown";
            add(buf, cap, &n, "  0x%08x - 0x%08x  %8u KiB  %s\n",
                e->base_lo, e->base_lo + e->length_lo,
                e->length_lo / 1024, type);
        }
    }
    return n;
}

static int generate_uptime(char *buf, int cap)
{
    u32 ms = timer_ms();
    return ksnprintf(buf, cap, "%u.%02u %u.%02u\n",
                     ms / 1000, (ms / 10) % 100, ms / 1000, (ms / 10) % 100);
}

static int generate_interrupts(char *buf, int cap)
{
    int n = 0;
    static const char *irq_names[16] = {
        "timer", "keyboard", "cascade", "com2", "com1", "lpt2", "floppy", "lpt1",
        "rtc", "acpi", "pci0", "pci1", "mouse", "fpu", "ide", "ide2"
    };
    add(buf, cap, &n, "           CPU0\n");
    add(buf, cap, &n, "  IDT base=0x%08x limit=0x%x\n", idt_base(), idt_limit());
    add(buf, cap, &n, "  %3s  %10s  %s\n", "IRQ", "COUNT", "DEVICE");
    for (int i = 0; i < 16; i++)
        add(buf, cap, &n, "  %3d  %10u  %s\n", i, irq_count(i), irq_names[i]);
    add(buf, cap, &n, "  total interrupts: %u\n", interrupt_total_count());
    add(buf, cap, &n, "  syscalls: %u\n", syscall_count());
    add(buf, cap, &n, "  PIC mask master=0x%02x slave=0x%02x\n",
        pic_get_mask_master(), pic_get_mask_slave());
    return n;
}

static int generate_pci(char *buf, int cap)
{
    int n = 0;
    pci_device_t devs[PCI_MAX_DEVICES];
    int count = pci_scan(devs, PCI_MAX_DEVICES);
    add(buf, cap, &n, "PCI devices: %d\n", count);
    add(buf, cap, &n, "BUS:SLOT.FN  VENDOR:DEVICE  CLASS  DESCRIPTION\n");
    for (int i = 0; i < count; i++) {
        add(buf, cap, &n, "%02x:%02x.%x      %04x:%04x      %02x%02x   %s\n",
            devs[i].bus, devs[i].slot, devs[i].func,
            devs[i].vendor, devs[i].device,
            devs[i].class_code, devs[i].subclass,
            pci_class_name(devs[i].class_code, devs[i].subclass));
    }
    return n;
}

static int generate_e820(char *buf, int cap)
{
    int n = 0;
    if (!g_boot_info) return 0;
    add(buf, cap, &n, "E820 physical memory map (%u entries)\n", g_boot_info->e820_count);
    add(buf, cap, &n, "BASE        LENGTH      TYPE\n");
    for (u32 i = 0; i < g_boot_info->e820_count && i < 16; i++) {
        e820_entry_t *e = &g_boot_info->e820[i];
        add(buf, cap, &n, "0x%08x  0x%08x  %u\n", e->base_lo, e->length_lo, e->type);
    }
    add(buf, cap, &n, "BIOS conventional: %u KiB\n", g_boot_info->mem_lower_kb);
    add(buf, cap, &n, "BIOS extended:     %u KiB\n", g_boot_info->mem_upper_kb);
    return n;
}

static int generate_syscalls(char *buf, int cap)
{
    int n = 0;
    add(buf, cap, &n, "INT 0x80 system calls (eax = number)\n");
    add(buf, cap, &n, "NUM  NAME        ARGS  DESCRIPTION\n");
    for (int i = 1; i < SYS_COUNT; i++) {
        const syscall_info_t *s = syscall_info(i);
        if (!s) continue;
        add(buf, cap, &n, "%3d  %-10s  %d     %s\n", i, s->name, s->args, s->description);
    }
    add(buf, cap, &n, "\ncalls since boot: %u\n", syscall_count());
    return n;
}

static int generate_sched(char *buf, int cap)
{
    int n = 0;
    sched_stats_t st;
    sched_stats(&st);
    add(buf, cap, &n, "policy: %s\n", sched_algo_name());
    add(buf, cap, &n, "ticks: %u  switches: %u  completed: %u\n",
        st.ticks, st.switches, st.completed);
    add(buf, cap, &n, "queue length: %d\n", st.queue_len);
    if (st.current)
        add(buf, cap, &n, "running: pid=%d %s\n", st.current->pid, st.current->name);
    else
        add(buf, cap, &n, "running: <idle>\n");
    add(buf, cap, &n, "\nREADY QUEUE\n");
    for (int i = 0; i < sched_queue_len(); i++) {
        pcb_t *p = sched_queue_at(i);
        add(buf, cap, &n, "  pid=%d %-16s prio=%d left=%d\n",
            p->pid, p->name, p->priority, p->burst_left);
    }
    return n;
}

static int generate_ps(char *buf, int cap)
{
    int n = 0;
    pcb_t *table = proc_table();
    add(buf, cap, &n, " PID  NAME              STATE    PRI  BURST  LEFT  CPU\n");
    for (int i = 0; i < PROC_TABLE_SIZE; i++) {
        pcb_t *p = &table[i];
        if (p->state == STATE_UNUSED) continue;
        add(buf, cap, &n, " %3d  %-16s  %-7s  %2d   %4d  %4d  %4u\n",
            p->pid, p->name, proc_state_name(p->state), p->priority,
            p->burst_total, p->burst_left, p->cpu_ticks);
    }
    return n;
}

int procfs_generate(const char *path, char *buf, int cap)
{
    if (!path || !buf || cap <= 0) return -1;
    const char *name = path;
    if (kstrstartswith(path, "/proc/")) name = path + 6;

    if (kstrcmp(name, "version") == 0) return generate_version(buf, cap);
    if (kstrcmp(name, "cpuinfo") == 0) return generate_cpuinfo(buf, cap);
    if (kstrcmp(name, "meminfo") == 0) return generate_meminfo(buf, cap);
    if (kstrcmp(name, "uptime") == 0) return generate_uptime(buf, cap);
    if (kstrcmp(name, "interrupts") == 0) return generate_interrupts(buf, cap);
    if (kstrcmp(name, "pci") == 0) return generate_pci(buf, cap);
    if (kstrcmp(name, "e820") == 0) return generate_e820(buf, cap);
    if (kstrcmp(name, "syscalls") == 0) return generate_syscalls(buf, cap);
    if (kstrcmp(name, "sched") == 0) return generate_sched(buf, cap);
    if (kstrcmp(name, "ps") == 0) return generate_ps(buf, cap);
    if (kstrcmp(name, "mounts") == 0)
        return ksnprintf(buf, cap, "nova_vfs / vfs rw 0 0\nproc /proc proc ro 0 0\n");
    if (kstrcmp(name, "cmdline") == 0)
        return ksnprintf(buf, cap, "console=ttyS0,115200 video=%ux%ux%u root=/dev/ram0\n",
                         g_boot_info ? g_boot_info->width : 0,
                         g_boot_info ? g_boot_info->height : 0,
                         g_boot_info ? g_boot_info->bpp : 0);
    return -1;
}
