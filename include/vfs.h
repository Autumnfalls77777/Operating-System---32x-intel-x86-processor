/* ============================================================================
 * include/vfs.h  -  Hierarchical in-memory virtual filesystem
 *
 * Nodes form a tree rooted at "/".  File contents are allocated from the
 * kernel heap.  Nodes under /proc are virtual: their contents are generated
 * on demand by the procfs module.
 * ============================================================================ */

#ifndef NOVA_VFS_H
#define NOVA_VFS_H

#include "types.h"

#define VFS_MAX_NODES     96
#define VFS_NAME_MAX      32
#define VFS_PATH_MAX      160
#define VFS_MAX_FILE_SIZE 8192

#define VFS_MODE_DIR      0x4000u
#define VFS_MODE_FILE     0x8000u

typedef struct {
    int  used;
    int  is_dir;
    int  is_proc;
    int  parent;
    char name[VFS_NAME_MAX];
    u32  size;
    u32  capacity;
    u32  created;     /* timer ticks */
    u32  modified;    /* timer ticks */
    u16  mode;        /* permissions + type bits */
    char *data;
} vfs_node_t;

int   vfs_init(void);

/* Path resolution.  `cwd` may be NULL (treated as "/").  Relative paths are
 * resolved against cwd.  Returns a node index or -1. */
int   vfs_lookup(const char *path, const char *cwd);
int   vfs_parent(int node);
int   vfs_root(void);
const char *vfs_name(int node);
int   vfs_is_dir(int node);
u32   vfs_size(int node);
u16   vfs_mode(int node);
u32   vfs_modified(int node);
int   vfs_path(int node, char *buf, int cap);

/* Directory listing: dirs first, then case-insensitive name order. */
int   vfs_list(int dir, int *out, int max);

int   vfs_mkdir(const char *path, const char *cwd);
int   vfs_create(const char *path, const char *cwd);
int   vfs_write_file(const char *path, const char *cwd, const char *data, u32 len, int append);
int   vfs_read_file(const char *path, const char *cwd, char *buf, int cap);
char *vfs_data(const char *path, const char *cwd, u32 *size);
int   vfs_unlink(const char *path, const char *cwd);
int   vfs_rename(const char *old_path, const char *new_path, const char *cwd);
int   vfs_copy(const char *src, const char *dst, const char *cwd);
int   vfs_chmod(const char *path, const char *cwd, u16 mode);

int   vfs_used_nodes(void);
u32   vfs_used_bytes(void);
void  vfs_format_mode(u16 mode, char *buf);
void  vfs_format_size(u32 size, char *buf, int cap);
const char *vfs_type_name(int node);

#endif /* NOVA_VFS_H */
