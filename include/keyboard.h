/* ============================================================================
 * include/keyboard.h  -  PS/2 keyboard driver (IRQ1, scancode set 1)
 * ============================================================================ */

#ifndef NOVA_KEYBOARD_H
#define NOVA_KEYBOARD_H

#include "types.h"
#include "keys.h"

#define KB_QUEUE_SIZE 64

void  keyboard_init(void);
void  keyboard_handle_scancode(u8 sc);
int   keyboard_poll(key_event_t *out);
u8    keyboard_modifiers(void);
int   keyboard_key_count(void);
u32   keyboard_irq_count(void);
void  keyboard_poll_raw(void);   /* drain the PS/2 controller */

#endif /* NOVA_KEYBOARD_H */
