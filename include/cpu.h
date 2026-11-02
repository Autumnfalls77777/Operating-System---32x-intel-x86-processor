/* ============================================================================
 * include/cpu.h  -  CPUID and processor identification
 * ============================================================================ */

#ifndef NOVA_CPU_H
#define NOVA_CPU_H

#include "types.h"

typedef struct {
    u32 max_leaf;
    u32 max_ext_leaf;
    char vendor[13];
    char brand[49];
    u32 family;
    u32 model;
    u32 stepping;
    u32 type;
    u32 features_edx;
    u32 features_ecx;
    u32 ext_features_edx;
    u32 ext_features_ecx;
} cpu_info_t;

void cpuid_info(cpu_info_t *out);
void cpuid_format_features(char *buf, int cap);
void cpuid_format_brand(char *buf, int cap);

static inline void cpuid(u32 leaf, u32 subleaf, u32 *a, u32 *b, u32 *c, u32 *d)
{
    __asm__ volatile ("cpuid"
                      : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                      : "a"(leaf), "c"(subleaf));
}

#endif /* NOVA_CPU_H */
