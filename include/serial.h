/* ============================================================================
 * include/serial.h  -  COM1 debug serial port
 * ============================================================================ */

#ifndef NOVA_SERIAL_H
#define NOVA_SERIAL_H

#include "types.h"

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);
void serial_write_len(const char *s, int len);
int  serial_printf(const char *fmt, ...);

#endif /* NOVA_SERIAL_H */
