/* ============================================================================
 * include/shell.h  -  NOVA shell interpreter
 * ============================================================================ */

#ifndef NOVA_SHELL_H
#define NOVA_SHELL_H

#include "terminal.h"

void shell_execute(terminal_t *t, const char *line);
void shell_welcome(terminal_t *t);
void shell_print_prompt(terminal_t *t);

#endif /* NOVA_SHELL_H */
