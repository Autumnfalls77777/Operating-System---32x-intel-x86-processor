/* ============================================================================
 * src/drivers/cpu.c  -  CPUID vendor, brand and feature detection
 * ============================================================================ */

#include "kernel.h"
#include "cpu.h"

void cpuid_info(cpu_info_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    u32 a, b, c, d;
    cpuid(0, 0, &a, &b, &c, &d);
    out->max_leaf = a;

    memcpy(out->vendor + 0, &b, 4);
    memcpy(out->vendor + 4, &d, 4);
    memcpy(out->vendor + 8, &c, 4);
    out->vendor[12] = '\0';

    if (a >= 1) {
        cpuid(1, 0, &a, &b, &c, &d);
        u32 base_family = (a >> 8) & 0xF;
        u32 ext_family  = (a >> 20) & 0xFF;
        u32 base_model  = (a >> 4) & 0xF;
        u32 ext_model   = (a >> 16) & 0xF;

        out->family = (base_family == 0xF) ? base_family + ext_family : base_family;
        out->model  = (base_family == 0xF || base_family == 6)
                    ? base_model + (ext_model << 4) : base_model;
        out->stepping = a & 0xF;
        out->type = (a >> 12) & 0x3;
        out->features_edx = d;
        out->features_ecx = c;
    }

    cpuid(0x80000000u, 0, &a, &b, &c, &d);
    out->max_ext_leaf = a;

    if (a >= 0x80000004u) {
        u32 *brand = (u32 *)out->brand;
        for (u32 leaf = 0; leaf < 3; leaf++) {
            cpuid(0x80000002u + leaf, 0, &brand[leaf * 4 + 0],
                  &brand[leaf * 4 + 1], &brand[leaf * 4 + 2],
                  &brand[leaf * 4 + 3]);
        }
        out->brand[48] = '\0';
        /* Trim leading spaces. */
        char *p = out->brand;
        while (*p == ' ') p++;
        if (p != out->brand) memmove(out->brand, p, kstrlen(p) + 1);
    } else {
        kstrncpy(out->brand, "Unknown x86 processor", sizeof(out->brand));
    }

    if (a >= 0x80000001u) {
        cpuid(0x80000001u, 0, &a, &b, &c, &d);
        out->ext_features_edx = d;
        out->ext_features_ecx = c;
    }
}

static void append(char *buf, int cap, int *len, const char *s)
{
    int n = kstrlen(s);
    if (*len + n + 2 >= cap) return;
    if (*len > 0) buf[(*len)++] = ' ';
    memcpy(buf + *len, s, (size_t)n);
    *len += n;
    buf[*len] = '\0';
}

void cpuid_format_features(char *buf, int cap)
{
    if (!buf || cap <= 0) return;
    cpu_info_t info;
    cpuid_info(&info);
    buf[0] = '\0';
    int len = 0;

    struct { u32 mask; const char *name; } edx_flags[] = {
        { 1u << 0, "fpu" }, { 1u << 4, "tsc" }, { 1u << 5, "msr" },
        { 1u << 6, "pae" }, { 1u << 8, "cx8" }, { 1u << 9, "apic" },
        { 1u << 11, "sep" }, { 1u << 12, "mtrr" }, { 1u << 13, "pge" },
        { 1u << 15, "cmov" }, { 1u << 16, "pat" }, { 1u << 19, "clflush" },
        { 1u << 23, "mmx" }, { 1u << 24, "fxsr" }, { 1u << 25, "sse" },
        { 1u << 26, "sse2" }, { 1u << 28, "htt" },
    };

    for (u32 i = 0; i < sizeof(edx_flags) / sizeof(edx_flags[0]); i++)
        if (info.features_edx & edx_flags[i].mask)
            append(buf, cap, &len, edx_flags[i].name);

    struct { u32 mask; const char *name; } ecx_flags[] = {
        { 1u << 0, "sse3" }, { 1u << 1, "pclmul" }, { 1u << 3, "monitor" },
        { 1u << 9, "ssse3" }, { 1u << 12, "fma" }, { 1u << 13, "cx16" },
        { 1u << 19, "sse4.1" }, { 1u << 20, "sse4.2" },
        { 1u << 23, "popcnt" }, { 1u << 25, "aes" }, { 1u << 28, "avx" },
        { 1u << 30, "rdrand" },
    };

    for (u32 i = 0; i < sizeof(ecx_flags) / sizeof(ecx_flags[0]); i++)
        if (info.features_ecx & ecx_flags[i].mask)
            append(buf, cap, &len, ecx_flags[i].name);

    if (info.ext_features_edx & (1u << 29))
        append(buf, cap, &len, "lm");
    if (info.ext_features_edx & (1u << 11))
        append(buf, cap, &len, "syscall");
    if (info.ext_features_edx & (1u << 20))
        append(buf, cap, &len, "nx");
}

void cpuid_format_brand(char *buf, int cap)
{
    cpu_info_t info;
    cpuid_info(&info);
    kstrncpy(buf, info.brand, cap);
}
