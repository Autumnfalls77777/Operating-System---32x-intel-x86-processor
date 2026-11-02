/* ============================================================================
 * src/drivers/serial.c  -  COM1 16550 UART debug output
 * ============================================================================ */

#include "kernel.h"
#include "serial.h"

#define COM1 0x3F8

void serial_init(void)
{
    port_outb(COM1 + 1, 0x00);   /* Disable interrupts            */
    port_outb(COM1 + 3, 0x80);   /* Enable DLAB                   */
    port_outb(COM1 + 0, 0x01);   /* Divisor low: 115200 baud      */
    port_outb(COM1 + 1, 0x00);   /* Divisor high                  */
    port_outb(COM1 + 3, 0x03);   /* 8 bits, no parity, 1 stop     */
    port_outb(COM1 + 2, 0xC7);   /* FIFO enable, clear, 14-byte   */
    port_outb(COM1 + 4, 0x0B);   /* IRQs enabled, RTS/DSR set     */
}

void serial_putc(char c)
{
    int guard = 100000;
    while (!(port_inb(COM1 + 5) & 0x20) && guard-- > 0) { }
    port_outb(COM1, (u8)c);
}

void serial_write(const char *s)
{
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

void serial_write_len(const char *s, int len)
{
    for (int i = 0; i < len; i++) {
        if (s[i] == '\n') serial_putc('\r');
        serial_putc(s[i]);
    }
}

int serial_printf(const char *fmt, ...)
{
    char buf[512];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = kvsnprintf(buf, (int)sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    serial_write(buf);
    return n;
}
