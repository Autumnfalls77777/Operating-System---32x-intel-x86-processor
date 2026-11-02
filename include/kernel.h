/* ============================================================================
 * include/kernel.h  -  Core kernel services: strings, formatting, logging
 * ============================================================================ */

#ifndef NOVA_KERNEL_H
#define NOVA_KERNEL_H

#include "types.h"
#include "config.h"
#include "io.h"

/* ── Memory / string routines (implemented in src/lib/string.c) ───────────── */

void  *memcpy(void *dst, const void *src, size_t n);
void  *memset(void *dst, int c, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
int    memcmp(const void *a, const void *b, size_t n);

int    kstrlen(const char *s);
int    kstrcmp(const char *a, const char *b);
int    kstrncmp(const char *a, const char *b, int n);
int    kstrcasecmp(const char *a, const char *b);
char  *kstrcpy(char *dst, const char *src);
char  *kstrncpy(char *dst, const char *src, int n);
char  *kstrcat(char *dst, const char *src);
char  *kstrncat(char *dst, const char *src, int n);
char  *kstrchr(const char *s, int c);
char  *kstrrchr(const char *s, int c);
char  *kstrstr(const char *haystack, const char *needle);
int    kstrstartswith(const char *s, const char *prefix);
void   kstrtrim(char *s);

/* Fast aligned dword copy used by the compositor. */
void   kfast_copy32(void *dst, const void *src, u32 dwords);
void   kfast_fill32(void *dst, u32 value, u32 dwords);

/* ── Formatted output ─────────────────────────────────────────────────────── */

int    kvsnprintf(char *buf, int cap, const char *fmt, __builtin_va_list ap);
int    ksnprintf(char *buf, int cap, const char *fmt, ...);
int    kprintf(const char *fmt, ...);            /* serial console */

/* ── Parsing helpers ──────────────────────────────────────────────────────── */

u32    kstrtoul(const char *s, char **end, int base);
int    katoi(const char *s);
int    kisdigit(int c);
int    ktoupper(int c);
int    ktolower(int c);

/* ── Kernel log ring buffer (also mirrored to COM1) ───────────────────────── */

#define KLOG_LEVEL_INFO  0
#define KLOG_LEVEL_OK    1
#define KLOG_LEVEL_WARN  2
#define KLOG_LEVEL_ERR   3
#define KLOG_LEVEL_BOOT  4

void        klog_init(void);
void        klog(int level, const char *fmt, ...);
int         klog_count(void);
const char *klog_line(int index, int *level);

/* ── Panic / fatal error screen ───────────────────────────────────────────── */

struct regs;
void panic(const char *fmt, ...) NORETURN;
void panic_regs(struct regs *r, const char *msg) NORETURN;

#define KASSERT(cond, msg) do { if (!(cond)) panic("assert failed: %s (%s:%d)", msg, __FILE__, __LINE__); } while (0)

#endif /* NOVA_KERNEL_H */
