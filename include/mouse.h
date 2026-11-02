/* ============================================================================
 * include/mouse.h  -  PS/2 mouse driver (IRQ12, IntelliMouse 4-byte packets)
 * ============================================================================ */

#ifndef NOVA_MOUSE_H
#define NOVA_MOUSE_H

#include "types.h"

#define MOUSE_BTN_LEFT   0x01
#define MOUSE_BTN_RIGHT  0x02
#define MOUSE_BTN_MIDDLE 0x04

typedef struct {
    int x;
    int y;
    int buttons;      /* MOUSE_BTN_* bitmask                    */
    int wheel;        /* accumulated wheel delta (positive = up) */
    int moved;        /* set when x/y changed since last poll   */
    int wheel_delta;  /* wheel delta consumed on next poll      */
    int packet_size;  /* 3 or 4 bytes (IntelliMouse)            */
    u32 irq_count;
} mouse_state_t;

void           mouse_init(int screen_w, int screen_h);
void           mouse_handle_byte(u8 byte);
mouse_state_t *mouse_get(void);
void           mouse_set_bounds(int w, int h);
void           mouse_poll(void);

#endif /* NOVA_MOUSE_H */
