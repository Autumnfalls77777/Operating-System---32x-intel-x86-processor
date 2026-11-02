/* ============================================================================
 * include/bootinfo.h  -  Hand-off structure written by the stage-2 bootloader
 *
 * The bootloader writes this at physical address 0x5000 before switching to
 * protected mode.  Keep the layout in sync with boot/stage2.asm.
 * ============================================================================ */

#ifndef NOVA_BOOTINFO_H
#define NOVA_BOOTINFO_H

#include "types.h"
#include "config.h"

#define BOOTINFO_MAGIC 0x4E4F5641u   /* 'NOVA' */

typedef struct PACKED {
    u32 base_lo;      /* Physical base address, low 32 bits  */
    u32 length_lo;    /* Region length, low 32 bits          */
    u32 type;         /* 1 = usable RAM, 2 = reserved, ...   */
} e820_entry_t;

typedef struct PACKED {
    u32 magic;            /* BOOTINFO_MAGIC                          */
    u32 fb_addr;          /* Linear framebuffer physical address     */
    u32 width;            /* Horizontal pixels                       */
    u32 height;           /* Vertical pixels                         */
    u32 pitch;            /* Bytes per scan line                     */
    u32 bpp;              /* Bits per pixel (24 or 32)                */
    u32 e820_count;       /* Number of valid entries below           */
    u32 mem_lower_kb;     /* Conventional memory (INT 12h)           */
    u32 mem_upper_kb;     /* Extended memory (INT 15h/E801), KB      */
    u32 boot_drive;       /* BIOS boot drive number                  */
    e820_entry_t e820[16];
} boot_info_t;

#endif /* NOVA_BOOTINFO_H */
