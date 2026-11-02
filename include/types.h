/* ============================================================================
 * include/types.h  -  Fixed-width integer types for the freestanding kernel
 * ============================================================================ */

#ifndef NOVA_TYPES_H
#define NOVA_TYPES_H

typedef unsigned char      u8;
typedef signed char        i8;
typedef unsigned short     u16;
typedef signed short       i16;
typedef unsigned int       u32;
typedef signed int         i32;
typedef unsigned long long u64;
typedef signed long long   i64;

typedef unsigned int       size_t;
typedef unsigned int       uintptr_t;

#define NULL ((void *)0)
#define true  1
#define false 0

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define clamp(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

/* Common function attributes */
#define PACKED      __attribute__((packed))
#define NORETURN    __attribute__((noreturn))
#define UNUSED(x)   ((void)(x))
#define ALIGNED(n)  __attribute__((aligned(n)))

#endif /* NOVA_TYPES_H */
