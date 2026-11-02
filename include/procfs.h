/* ============================================================================
 * include/procfs.h  -  Generated /proc virtual files
 * ============================================================================ */

#ifndef NOVA_PROCFS_H
#define NOVA_PROCFS_H

#include "types.h"

/* Generates the contents of a /proc virtual file.  Returns the number of
 * bytes written (excluding the terminator), or -1 if the path is unknown. */
int procfs_generate(const char *path, char *buf, int cap);

/* Read-only /proc file names, used by vfs_init(). */
extern const char *procfs_files[];
extern const int   procfs_file_count;

#endif /* NOVA_PROCFS_H */
