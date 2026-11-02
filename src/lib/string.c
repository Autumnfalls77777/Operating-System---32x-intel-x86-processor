/* ============================================================================
 * src/lib/string.c  -  Freestanding string, memory and formatting routines
 * ============================================================================ */

#include "kernel.h"
#include "serial.h"

/* ── Memory primitives (also satisfy compiler-generated calls) ────────────── */

void *memcpy(void *dst, const void *src, size_t n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;

    if ((((u32)d | (u32)s) & 3u) == 0) {
        u32 *dw = (u32 *)d;
        const u32 *sw = (const u32 *)s;
        u32 count = n >> 2;
        while (count--) *dw++ = *sw++;
        d = (u8 *)dw;
        s = (const u8 *)sw;
        n &= 3;
    }
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    u8 *d = (u8 *)dst;
    u8 value = (u8)c;
    if (((u32)d & 3u) == 0) {
        u32 word = (u32)value * 0x01010101u;
        u32 *dw = (u32 *)d;
        u32 count = n >> 2;
        while (count--) *dw++ = word;
        d = (u8 *)dw;
        n &= 3;
    }
    while (n--) *d++ = value;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const u8 *pa = (const u8 *)a;
    const u8 *pb = (const u8 *)b;
    while (n--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}

void kfast_copy32(void *dst, const void *src, u32 dwords)
{
    __asm__ volatile ("cld; rep movsl"
                      : "+D"(dst), "+S"(src), "+c"(dwords)
                      :
                      : "memory");
}

void kfast_fill32(void *dst, u32 value, u32 dwords)
{
    __asm__ volatile ("cld; rep stosl"
                      : "+D"(dst), "+c"(dwords)
                      : "a"(value)
                      : "memory");
}

/* ── Strings ──────────────────────────────────────────────────────────────── */

int kstrlen(const char *s)
{
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

int kstrcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) { a++; b++; }
    return (int)(u8)*a - (int)(u8)*b;
}

int kstrncmp(const char *a, const char *b, int n)
{
    while (n-- > 0 && *a && (*a == *b)) { a++; b++; }
    if (n < 0) return 0;
    return (int)(u8)*a - (int)(u8)*b;
}

int kstrcasecmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = ktolower((u8)*a);
        int cb = ktolower((u8)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return ktolower((u8)*a) - ktolower((u8)*b);
}

char *kstrcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

char *kstrncpy(char *dst, const char *src, int n)
{
    int i = 0;
    for (; i < n - 1 && src[i]; i++) dst[i] = src[i];
    if (n > 0) dst[i] = '\0';
    return dst;
}

char *kstrcat(char *dst, const char *src)
{
    char *d = dst;
    while (*d) d++;
    while ((*d++ = *src++));
    return dst;
}

char *kstrncat(char *dst, const char *src, int n)
{
    char *d = dst;
    while (*d) d++;
    while (n-- > 0 && *src) *d++ = *src++;
    *d = '\0';
    return dst;
}

char *kstrchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : NULL;
}

char *kstrrchr(const char *s, int c)
{
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == 0) return (char *)s;
    return (char *)last;
}

char *kstrstr(const char *haystack, const char *needle)
{
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char *)haystack;
    }
    return NULL;
}

int kstrstartswith(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++; prefix++;
    }
    return 1;
}

void kstrtrim(char *s)
{
    char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (p != s) memmove(s, p, kstrlen(p) + 1);

    int len = kstrlen(s);
    while (len > 0) {
        char c = s[len - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        s[--len] = '\0';
    }
}

int kisdigit(int c) { return c >= '0' && c <= '9'; }
int ktoupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int ktolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

u32 kstrtoul(const char *s, char **end, int base)
{
    u32 value = 0;
    const char *p = s;

    while (*p == ' ' || *p == '\t') p++;

    if (base == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            base = 16;
            p += 2;
        } else if (p[0] == '0') {
            base = 8;
            p++;
        } else {
            base = 10;
        }
    } else if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }

    for (;;) {
        int c = (u8)*p;
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else break;
        if (digit >= base) break;
        value = value * (u32)base + (u32)digit;
        p++;
    }

    if (end) *end = (char *)p;
    return value;
}

int katoi(const char *s)
{
    int sign = 1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    int value = 0;
    while (kisdigit((u8)*s)) {
        value = value * 10 + (*s - '0');
        s++;
    }
    return value * sign;
}

/* ── Formatted output ─────────────────────────────────────────────────────── */

static int fmt_utoa(u32 value, int base, int upper, char *buf)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[32];
    int n = 0;
    if (value == 0) tmp[n++] = '0';
    while (value) {
        tmp[n++] = digits[value % (u32)base];
        value /= (u32)base;
    }
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    return n;
}

int kvsnprintf(char *buf, int cap, const char *fmt, __builtin_va_list ap)
{
    if (!buf || cap <= 0) return 0;

    int written = 0;
    int limit = cap - 1;

    while (*fmt) {
        if (*fmt != '%') {
            if (written < limit) buf[written] = *fmt;
            written++;
            fmt++;
            continue;
        }

        fmt++;
        int left = 0;
        int zero = 0;
        int width = 0;

        while (*fmt == '-' || *fmt == '0' || *fmt == '+' || *fmt == ' ') {
            if (*fmt == '-') left = 1;
            if (*fmt == '0') zero = 1;
            fmt++;
        }
        while (kisdigit((u8)*fmt)) {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        if (*fmt == 'l') {
            /* Long is the same width as int on i386 ILP32. */
            fmt++;
            if (*fmt == 'l') fmt++;
        }

        char spec = *fmt++;
        char temp[64];
        int len = 0;
        int numeric = 0;
        int negative = 0;
        u32 number = 0;

        switch (spec) {
        case '%':
            temp[len++] = '%';
            break;
        case 'c':
            temp[len++] = (char)__builtin_va_arg(ap, int);
            break;
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            len = kstrlen(s);
            if (len > (int)sizeof(temp) - 1) len = (int)sizeof(temp) - 1;
            memcpy(temp, s, (size_t)len);
            break;
        }
        case 'd':
        case 'i': {
            int v = __builtin_va_arg(ap, int);
            numeric = 1;
            if (v < 0) {
                negative = 1;
                number = (u32)(-(v + 1)) + 1u;
            } else {
                number = (u32)v;
            }
            len = fmt_utoa(number, 10, 0, temp);
            break;
        }
        case 'u':
            numeric = 1;
            number = __builtin_va_arg(ap, u32);
            len = fmt_utoa(number, 10, 0, temp);
            break;
        case 'x':
            numeric = 1;
            number = __builtin_va_arg(ap, u32);
            len = fmt_utoa(number, 16, 0, temp);
            break;
        case 'X':
            numeric = 1;
            number = __builtin_va_arg(ap, u32);
            len = fmt_utoa(number, 16, 1, temp);
            break;
        case 'p': {
            u32 p = __builtin_va_arg(ap, u32);
            temp[len++] = '0';
            temp[len++] = 'x';
            int start = len;
            len += fmt_utoa(p, 16, 0, temp + len);
            while (len - start < 8) {
                for (int i = len; i > start; i--) temp[i] = temp[i - 1];
                temp[start] = '0';
                len++;
            }
            break;
        }
        default:
            temp[len++] = '%';
            temp[len++] = spec;
            break;
        }

        int total = len;
        if (numeric && negative) total++;
        int pad = width > total ? width - total : 0;

        if (!left) {
            if (negative && zero) {
                if (written < limit) buf[written] = '-';
                written++;
                negative = 0;
            }
            for (int i = 0; i < pad; i++) {
                if (written < limit) buf[written] = zero ? '0' : ' ';
                written++;
            }
        }
        if (numeric && negative) {
            if (written < limit) buf[written] = '-';
            written++;
        }
        for (int i = 0; i < len; i++) {
            if (written < limit) buf[written] = temp[i];
            written++;
        }
        if (left) {
            for (int i = 0; i < pad; i++) {
                if (written < limit) buf[written] = ' ';
                written++;
            }
        }
    }

    if (cap > 0) buf[min(written, limit)] = '\0';
    return written;
}

int ksnprintf(char *buf, int cap, const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = kvsnprintf(buf, cap, fmt, ap);
    __builtin_va_end(ap);
    return n;
}

int kprintf(const char *fmt, ...)
{
    char buf[512];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = kvsnprintf(buf, (int)sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    serial_write(buf);
    return n;
}
