/* ============================================================================
 * src/drivers/mouse.c  -  PS/2 mouse (IntelliMouse wheel support)
 * ============================================================================ */

#include "kernel.h"
#include "mouse.h"
#include "keyboard.h"
#include "interrupts.h"

#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_CMD     0x64

static mouse_state_t state;
static int screen_w = 1024;
static int screen_h = 768;

static int wait_write(void)
{
    for (int i = 0; i < 200000; i++) {
        if (!(port_inb(PS2_STATUS) & 0x02)) return 1;
        cpu_pause();
    }
    return 0;
}

static int wait_read(void)
{
    for (int i = 0; i < 200000; i++) {
        if (port_inb(PS2_STATUS) & 0x01) return 1;
        cpu_pause();
    }
    return 0;
}

static u8 ps2_read(void)
{
    if (!wait_read()) return 0xFF;
    return port_inb(PS2_DATA);
}

static void ps2_write_cmd(u8 cmd)
{
    if (!wait_write()) return;
    port_outb(PS2_CMD, cmd);
}

static void mouse_write(u8 value)
{
    if (!wait_write()) return;
    port_outb(PS2_CMD, 0xD4);
    if (!wait_write()) return;
    port_outb(PS2_DATA, value);
    (void)ps2_read();        /* consume ACK */
}

void mouse_set_bounds(int w, int h)
{
    if (w > 0) screen_w = w;
    if (h > 0) screen_h = h;
    if (state.x >= screen_w) state.x = screen_w - 1;
    if (state.y >= screen_h) state.y = screen_h - 1;
    if (state.x < 0) state.x = 0;
    if (state.y < 0) state.y = 0;
}

void mouse_init(int w, int h)
{
    memset(&state, 0, sizeof(state));
    state.x = (w > 0) ? w / 2 : 512;
    state.y = (h > 0) ? h / 2 : 384;
    state.packet_size = 3;
    screen_w = w;
    screen_h = h;

    /* Enable the auxiliary device and its IRQ line. */
    ps2_write_cmd(0xA8);

    ps2_write_cmd(0x20);
    u8 cmd_byte = ps2_read();
    cmd_byte |= 0x02;       /* enable IRQ12 */
    cmd_byte &= (u8)~0x20;  /* enable aux clock */
    ps2_write_cmd(0x60);
    if (wait_write()) port_outb(PS2_DATA, cmd_byte);

    /* IntelliMouse magic: sample rates 200, 100, 80 then read the ID. */
    mouse_write(0xF3); mouse_write(200);
    mouse_write(0xF3); mouse_write(100);
    mouse_write(0xF3); mouse_write(80);

    mouse_write(0xF2);
    u8 id = ps2_read();
    if (id == 0x03) state.packet_size = 4;

    mouse_write(0xF3); mouse_write(100);   /* 100 samples/sec */
    mouse_write(0xE8); mouse_write(3);     /* 8 counts/mm     */
    mouse_write(0xF4);                     /* enable reporting */

    /* Drain any remaining configuration bytes. */
    int guard = 32;
    while ((port_inb(PS2_STATUS) & 0x01) && guard-- > 0)
        (void)port_inb(PS2_DATA);

    klog(KLOG_LEVEL_INFO, "PS/2 mouse ready (%d-byte packets)", state.packet_size);
}

void mouse_handle_byte(u8 byte)
{
    static u8 packet[4];
    static int index;

    if (index == 0) {
        if (!(byte & 0x08)) return;   /* packet sync bit */
        packet[0] = byte;
        index = 1;
        return;
    }

    packet[index++] = byte;
    if (index < state.packet_size) return;

    u8 flags = packet[0];
    int dx = packet[1];
    int dy = packet[2];
    if (flags & 0x10) dx -= 256;
    if (flags & 0x20) dy -= 256;

    int new_x = state.x + dx;
    int new_y = state.y - dy;
    if (new_x < 0) new_x = 0;
    if (new_x >= screen_w) new_x = screen_w - 1;
    if (new_y < 0) new_y = 0;
    if (new_y >= screen_h) new_y = screen_h - 1;

    if (new_x != state.x || new_y != state.y) state.moved = 1;
    state.x = new_x;
    state.y = new_y;
    state.buttons = flags & 0x07;

    if (state.packet_size == 4) {
        int wheel = packet[3] & 0x0F;
        if (wheel & 0x08) wheel -= 16;
        state.wheel_delta += wheel;
        state.wheel += wheel;
    }

    state.irq_count++;
    index = 0;
}

mouse_state_t *mouse_get(void)
{
    return &state;
}

void mouse_poll(void)
{
    keyboard_poll_raw();
}
