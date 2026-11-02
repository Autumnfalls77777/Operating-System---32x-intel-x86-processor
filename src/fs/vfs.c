/* ============================================================================
 * src/fs/vfs.c  -  Hierarchical in-memory virtual filesystem
 *
 * The filesystem is a tree of fixed-size nodes.  File data lives in the
 * kernel heap, so the static footprint stays small while files can be up to
 * VFS_MAX_FILE_SIZE bytes.  Nodes under /proc are generated on demand.
 * ============================================================================ */

#include "kernel.h"
#include "vfs.h"
#include "memory.h"
#include "timer.h"
#include "procfs.h"

static vfs_node_t nodes[VFS_MAX_NODES];
static char proc_buffer[4096];
static int  root_node;

static int node_alloc(int parent, const char *name, int is_dir, int is_proc)
{
    for (int i = 0; i < VFS_MAX_NODES; i++) {
        if (!nodes[i].used) {
            nodes[i].used = 1;
            nodes[i].is_dir = is_dir;
            nodes[i].is_proc = is_proc;
            nodes[i].parent = parent;
            nodes[i].size = 0;
            nodes[i].capacity = 0;
            nodes[i].created = timer_ticks();
            nodes[i].modified = nodes[i].created;
            nodes[i].mode = (u16)((is_dir ? VFS_MODE_DIR : VFS_MODE_FILE) |
                                  (is_dir ? 0755 : 0644));
            nodes[i].data = NULL;
            kstrncpy(nodes[i].name, name, VFS_NAME_MAX);
            return i;
        }
    }
    return -1;
}

static int find_child(int dir, const char *name)
{
    if (dir < 0 || dir >= VFS_MAX_NODES) return -1;
    for (int i = 0; i < VFS_MAX_NODES; i++) {
        if (nodes[i].used && nodes[i].parent == dir &&
            kstrcmp(nodes[i].name, name) == 0)
            return i;
    }
    return -1;
}

int vfs_root(void) { return root_node; }
int vfs_parent(int node) { return (node >= 0 && node < VFS_MAX_NODES) ? nodes[node].parent : -1; }
const char *vfs_name(int node) { return (node >= 0 && node < VFS_MAX_NODES) ? nodes[node].name : ""; }
int vfs_is_dir(int node) { return (node >= 0 && node < VFS_MAX_NODES) && nodes[node].is_dir; }
u32 vfs_size(int node) { return (node >= 0 && node < VFS_MAX_NODES) ? nodes[node].size : 0; }
u16 vfs_mode(int node) { return (node >= 0 && node < VFS_MAX_NODES) ? nodes[node].mode : 0; }
u32 vfs_modified(int node) { return (node >= 0 && node < VFS_MAX_NODES) ? nodes[node].modified : 0; }

int vfs_path(int node, char *buf, int cap)
{
    if (!buf || cap <= 0) return -1;
    if (node < 0 || node >= VFS_MAX_NODES || !nodes[node].used) {
        buf[0] = '\0';
        return -1;
    }
    if (node == root_node) {
        kstrncpy(buf, "/", cap);
        return 1;
    }

    int stack[VFS_MAX_NODES];
    int count = 0;
    int cur = node;
    while (cur >= 0 && cur != root_node && count < VFS_MAX_NODES) {
        stack[count++] = cur;
        cur = nodes[cur].parent;
    }

    int pos = 0;
    if (cap > 1) buf[pos++] = '/';
    for (int i = count - 1; i >= 0; i--) {
        const char *name = nodes[stack[i]].name;
        int len = kstrlen(name);
        if (pos + len + 2 >= cap) break;
        for (int j = 0; j < len; j++) buf[pos++] = name[j];
        if (i > 0) buf[pos++] = '/';
    }
    buf[pos] = '\0';
    return pos;
}

int vfs_lookup(const char *path, const char *cwd)
{
    if (!path || !*path) {
        if (cwd && *cwd) return vfs_lookup(cwd, "/");
        return root_node;
    }

    int cur;
    const char *p = path;

    if (*p == '/') {
        cur = root_node;
        while (*p == '/') p++;
    } else {
        cur = (cwd && *cwd) ? vfs_lookup(cwd, "/") : root_node;
        if (cur < 0) return -1;
    }

    char component[VFS_NAME_MAX];

    while (*p) {
        int n = 0;
        while (*p && *p != '/' && n < VFS_NAME_MAX - 1) component[n++] = *p++;
        component[n] = '\0';
        while (*p == '/') p++;

        if (n == 0) break;
        if (kstrcmp(component, ".") == 0) continue;
        if (kstrcmp(component, "..") == 0) {
            if (nodes[cur].parent >= 0) cur = nodes[cur].parent;
            continue;
        }

        if (!nodes[cur].is_dir) return -1;
        int next = find_child(cur, component);
        if (next < 0) return -1;
        cur = next;
    }

    return cur;
}

int vfs_list(int dir, int *out, int max)
{
    if (dir < 0 || dir >= VFS_MAX_NODES || !nodes[dir].is_dir || max <= 0)
        return 0;

    int count = 0;
    for (int i = 0; i < VFS_MAX_NODES && count < max; i++) {
        if (nodes[i].used && nodes[i].parent == dir)
            out[count++] = i;
    }

    /* Insertion sort: directories first, then case-insensitive name. */
    for (int i = 1; i < count; i++) {
        int key = out[i];
        int j = i - 1;
        while (j >= 0) {
            int a = out[j];
            int a_dir = nodes[a].is_dir;
            int b_dir = nodes[key].is_dir;
            int swap;
            if (a_dir != b_dir) {
                swap = (b_dir && !a_dir);
            } else {
                swap = kstrcasecmp(nodes[key].name, nodes[a].name) < 0;
            }
            if (!swap) break;
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    return count;
}

/* Split a path into its parent directory and final component. */
static int split_parent(const char *path, const char *cwd, int *parent,
                        char *name, int name_cap)
{
    if (!path || !*path || !name || name_cap <= 0) return -1;

    char work[VFS_PATH_MAX];
    kstrncpy(work, path, sizeof(work));

    int len = kstrlen(work);
    while (len > 1 && work[len - 1] == '/') work[--len] = '\0';
    if (kstrcmp(work, "/") == 0) return -1;

    char *slash = kstrrchr(work, '/');
    if (!slash) {
        *parent = (cwd && *cwd) ? vfs_lookup(cwd, "/") : root_node;
        kstrncpy(name, work, name_cap);
    } else if (slash == work) {
        *parent = root_node;
        kstrncpy(name, slash + 1, name_cap);
    } else {
        *slash = '\0';
        *parent = vfs_lookup(work, cwd);
        kstrncpy(name, slash + 1, name_cap);
    }

    if (*parent < 0 || !nodes[*parent].is_dir || !name[0]) return -1;
    return 0;
}

int vfs_mkdir(const char *path, const char *cwd)
{
    if (vfs_lookup(path, cwd) >= 0) return -1;

    int parent;
    char name[VFS_NAME_MAX];
    if (split_parent(path, cwd, &parent, name, sizeof(name)) < 0) return -1;

    int idx = node_alloc(parent, name, 1, 0);
    if (idx < 0) return -1;
    return idx;
}

int vfs_create(const char *path, const char *cwd)
{
    int existing = vfs_lookup(path, cwd);
    if (existing >= 0) return nodes[existing].is_dir ? -1 : existing;

    int parent;
    char name[VFS_NAME_MAX];
    if (split_parent(path, cwd, &parent, name, sizeof(name)) < 0) return -1;

    return node_alloc(parent, name, 0, 0);
}

int vfs_write_file(const char *path, const char *cwd, const char *data,
                   u32 len, int append)
{
    if (!data && len > 0) return -1;
    if (len > VFS_MAX_FILE_SIZE - 1) len = VFS_MAX_FILE_SIZE - 1;

    int idx = vfs_lookup(path, cwd);
    if (idx < 0) {
        idx = vfs_create(path, cwd);
        if (idx < 0) return -1;
    }
    if (nodes[idx].is_dir || nodes[idx].is_proc) return -1;

    u32 base = append ? nodes[idx].size : 0;
    u32 total = base + len;
    if (total > VFS_MAX_FILE_SIZE - 1) total = VFS_MAX_FILE_SIZE - 1;
    u32 new_len = total - base;

    char *new_data = (char *)kmalloc(total + 1);
    if (!new_data) return -1;

    if (base > 0 && nodes[idx].data) memcpy(new_data, nodes[idx].data, base);
    if (new_len > 0) memcpy(new_data + base, data, new_len);
    new_data[total] = '\0';

    if (nodes[idx].data) kfree(nodes[idx].data);
    nodes[idx].data = new_data;
    nodes[idx].size = total;
    nodes[idx].capacity = total + 1;
    nodes[idx].modified = timer_ticks();
    return (int)total;
}

int vfs_read_file(const char *path, const char *cwd, char *buf, int cap)
{
    if (!buf || cap <= 0) return -1;
    int idx = vfs_lookup(path, cwd);
    if (idx < 0 || nodes[idx].is_dir) return -1;

    if (nodes[idx].is_proc) {
        char full[VFS_PATH_MAX];
        vfs_path(idx, full, sizeof(full));
        int n = procfs_generate(full, buf, cap);
        if (n < 0) return -1;
        if (n >= cap) n = cap - 1;
        buf[n] = '\0';
        return n;
    }

    int n = (int)min(nodes[idx].size, (u32)cap - 1);
    if (n > 0 && nodes[idx].data) memcpy(buf, nodes[idx].data, (size_t)n);
    buf[n] = '\0';
    return n;
}

char *vfs_data(const char *path, const char *cwd, u32 *size)
{
    int idx = vfs_lookup(path, cwd);
    if (idx < 0 || nodes[idx].is_dir) return NULL;

    if (nodes[idx].is_proc) {
        char full[VFS_PATH_MAX];
        vfs_path(idx, full, sizeof(full));
        int n = procfs_generate(full, proc_buffer, sizeof(proc_buffer));
        if (n < 0) return NULL;
        if (size) *size = (u32)n;
        return proc_buffer;
    }

    if (size) *size = nodes[idx].size;
    return nodes[idx].data ? nodes[idx].data : (char *)"";
}

int vfs_unlink(const char *path, const char *cwd)
{
    int idx = vfs_lookup(path, cwd);
    if (idx < 0 || idx == root_node || !nodes[idx].used) return -1;

    if (nodes[idx].is_dir) {
        for (int i = 0; i < VFS_MAX_NODES; i++)
            if (nodes[i].used && nodes[i].parent == idx) return -1;
    }

    if (nodes[idx].data) kfree(nodes[idx].data);
    memset(&nodes[idx], 0, sizeof(vfs_node_t));
    return 0;
}

int vfs_rename(const char *old_path, const char *new_path, const char *cwd)
{
    int idx = vfs_lookup(old_path, cwd);
    if (idx < 0 || idx == root_node) return -1;
    if (vfs_lookup(new_path, cwd) >= 0) return -1;

    int parent;
    char name[VFS_NAME_MAX];
    if (split_parent(new_path, cwd, &parent, name, sizeof(name)) < 0) return -1;

    nodes[idx].parent = parent;
    kstrncpy(nodes[idx].name, name, VFS_NAME_MAX);
    nodes[idx].modified = timer_ticks();
    return 0;
}

int vfs_copy(const char *src, const char *dst, const char *cwd)
{
    u32 size;
    char *data = vfs_data(src, cwd, &size);
    if (!data) return -1;
    if (vfs_write_file(dst, cwd, data, size, 0) < 0) return -1;
    return 0;
}

int vfs_chmod(const char *path, const char *cwd, u16 mode)
{
    int idx = vfs_lookup(path, cwd);
    if (idx < 0) return -1;
    u16 type = (u16)(nodes[idx].mode & 0xF000u);
    nodes[idx].mode = (u16)(type | (mode & 0x0FFF));
    nodes[idx].modified = timer_ticks();
    return 0;
}

int vfs_used_nodes(void)
{
    int n = 0;
    for (int i = 0; i < VFS_MAX_NODES; i++) if (nodes[i].used) n++;
    return n;
}

u32 vfs_used_bytes(void)
{
    u32 n = 0;
    for (int i = 0; i < VFS_MAX_NODES; i++)
        if (nodes[i].used && !nodes[i].is_dir && !nodes[i].is_proc)
            n += nodes[i].size;
    return n;
}

void vfs_format_mode(u16 mode, char *buf)
{
    buf[0] = (mode & VFS_MODE_DIR) ? 'd' : '-';
    const char *rwx = "rwx";
    for (int i = 0; i < 9; i++) {
        int bit = 1 << (8 - i);
        buf[i + 1] = (mode & bit) ? rwx[i % 3] : '-';
    }
    buf[10] = '\0';
}

void vfs_format_size(u32 size, char *buf, int cap)
{
    if (size < 1024) {
        ksnprintf(buf, cap, "%u B", size);
    } else if (size < 1024u * 1024u) {
        u32 tenths = (size * 10u) / 1024u;
        ksnprintf(buf, cap, "%u.%u KB", tenths / 10u, tenths % 10u);
    } else {
        u32 tenths = (size * 10u) / (1024u * 1024u);
        ksnprintf(buf, cap, "%u.%u MB", tenths / 10u, tenths % 10u);
    }
}

const char *vfs_type_name(int node)
{
    if (node < 0 || node >= VFS_MAX_NODES) return "?";
    if (nodes[node].is_dir) return "dir";
    if (nodes[node].is_proc) return "proc";
    return "file";
}

/* â”€â”€ Seed filesystem â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */

static void seed_file(const char *path, const char *content)
{
    vfs_write_file(path, "/", content, (u32)kstrlen(content), 0);
}

int vfs_init(void)
{
    memset(nodes, 0, sizeof(nodes));
    root_node = node_alloc(-1, "/", 1, 0);
    if (root_node < 0) return -1;

    const char *dirs[] = {
        "/bin", "/dev", "/etc", "/home", "/home/dev", "/proc",
        "/tmp", "/usr", "/usr/include", "/usr/share", "/var", "/var/log"
    };
    for (u32 i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
        vfs_mkdir(dirs[i], "/");

    seed_file("/etc/motd",
        "Welcome to Altitude OS - a programmer-first operating system.\n"
        "Everything here is inspectable: kernel, heap, scheduler, VFS.\n"
        "Type 'help' in the terminal, or open DevTools to explore.\n");

    seed_file("/etc/os-release",
        "NAME=\"Altitude OS\"\n"
        "VERSION=\"1.0.0 (Aurora)\"\n"
        "ID=nova\n"
        "PRETTY_NAME=\"Altitude OS 1.0.0\"\n"
        "ANSI_COLOR=\"38;2;56;189;248\"\n");

    seed_file("/etc/nova.conf",
        "# Altitude OS user preferences\n"
        "accent=cyan\n"
        "wallpaper=aurora\n"
        "animations=1\n"
        "show_fps=0\n"
        "dock_magnify=1\n"
        "clock_seconds=0\n");

    seed_file("/home/dev/README.md",
        "# Welcome, developer\n"
        "\n"
        "Altitude OS is designed so that the kernel is not a black box.\n"
        "Use the terminal and DevTools to inspect live state:\n"
        "\n"
        "  help              list shell commands\n"
        "  neofetch          system summary\n"
        "  ps                process table\n"
        "  sched rr          switch CPU scheduler\n"
        "  meminfo           heap map\n"
        "  tree /            filesystem tree\n"
        "  cat /proc/cpuinfo generated CPU information\n"
        "  hexdump /home/dev/hello.c\n"
        "  syscalls          INT 0x80 table\n"
        "\n"
        "Try 'sh /home/dev/hello.sh' to run a shell script.\n");

    seed_file("/home/dev/hello.c",
        "/* NOVA kernel module example.\n"
        " * Programs are compiled into the kernel image with the\n"
        " * freestanding toolchain; this file is a playground. */\n"
        "\n"
        "#include \"kernel.h\"\n"
        "#include \"syscall.h\"\n"
        "\n"
        "void hello_module(void)\n"
        "{\n"
        "    const char *msg = \"hello from a NOVA module\\n\";\n"
        "    sys_call(SYS_WRITE, 2, (u32)msg, kstrlen(msg), 0);\n"
        "    pcb_t *p = proc_spawn(\"hello\", 3, 4);\n"
        "    sys_call(SYS_WRITE, 1, (u32)p->name, kstrlen(p->name), 0);\n"
        "}\n");

    seed_file("/home/dev/syscall.h",
        "#ifndef NOVA_SYSCALL_H\n"
        "#define NOVA_SYSCALL_H\n"
        "enum { SYS_WRITE=1, SYS_READ, SYS_GETPID, SYS_UPTIME,\n"
        "       SYS_TICKS, SYS_ALLOC, SYS_FREE, SYS_OPEN };\n"
        "u32 sys_call(u32 num, u32 a, u32 b, u32 c, u32 d);\n"
        "#endif\n");

    seed_file("/home/dev/hello.sh",
        "# NOVA shell script\n"
        "echo Hello from a NOVA shell script!\n"
        "uname\n"
        "uptime\n"
        "echo ---\n"
        "ls /\n"
        "echo ---\n"
        "ps\n");

    seed_file("/home/dev/notes.md",
        "# Scratch notes\n"
        "\n"
        "- Open Files and edit this with the built-in editor.\n"
        "- Ctrl+S saves to the RAM filesystem.\n"
        "- Settings are stored in /etc/nova.conf.\n"
        "- The DevTools app exposes registers, IDT, PCI and heap state.\n");

    seed_file("/usr/share/apps.txt",
        "terminal    interactive shell and developer console\n"
        "files       browse the NOVA virtual filesystem\n"
        "editor      edit text files with Ctrl+S save\n"
        "monitor     live system monitor and graphs\n"
        "process     process table and scheduler lab\n"
        "calculator  integer expression calculator\n"
        "devtools    kernel explorer (registers, IDT, PCI, heap)\n"
        "settings    appearance, wallpaper, power\n"
        "about       about Altitude OS\n"
        "help        shortcuts and developer guide\n");

    seed_file("/bin/README",
        "NOVA applications are C modules linked directly into the kernel\n"
        "image.  There is no ELF loader yet, but the app layer, window\n"
        "manager and syscall ABI are designed to make one possible.\n");

    seed_file("/var/log/README",
        "Kernel log lines are available through 'dmesg' and the DevTools\n"
        "Log tab.  A persistent disk filesystem is on the roadmap.\n");

    /* /proc virtual files */
    for (int i = 0; i < procfs_file_count; i++) {
        char path[VFS_PATH_MAX];
        ksnprintf(path, sizeof(path), "/proc/%s", procfs_files[i]);
        int parent = vfs_lookup("/proc", "/");
        int idx = node_alloc(parent, procfs_files[i], 0, 1);
        UNUSED(idx);
    }

    return 0;
}
