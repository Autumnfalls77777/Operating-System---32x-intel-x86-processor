/* ============================================================================
 * include/keys.h  -  Keyboard event model shared by drivers, WM and apps
 * ============================================================================ */

#ifndef NOVA_KEYS_H
#define NOVA_KEYS_H

#include "types.h"

/* Modifier bits carried with every key event. */
#define MOD_SHIFT   0x01
#define MOD_CTRL    0x02
#define MOD_ALT     0x04
#define MOD_CAPS    0x08
#define MOD_SUPER   0x10

/* Special (non-ASCII) key codes.  Printable keys set `ch` instead and leave
 * `key` as KEY_NONE.  Control keys also set their ASCII equivalent in `ch`. */
enum {
    KEY_NONE = 0,
    KEY_ESC,
    KEY_BACKSPACE,
    KEY_TAB,
    KEY_ENTER,
    KEY_INSERT,
    KEY_DELETE,
    KEY_HOME,
    KEY_END,
    KEY_PAGEUP,
    KEY_PAGEDOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_UP,
    KEY_DOWN,
    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10,
    KEY_F11,
    KEY_F12,
    KEY_SUPER,
    KEY_MENU,
    KEY_CAPS,
    KEY_PRINTSCREEN
};

typedef struct {
    u8 ch;      /* ASCII character, 0 for non-printable keys          */
    u8 key;     /* KEY_* code, KEY_NONE for printable characters     */
    u8 mods;    /* MOD_* bitmask captured at press time              */
    u8 pad;
} key_event_t;

#endif /* NOVA_KEYS_H */
