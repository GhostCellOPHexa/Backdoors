/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_SHELL_HISTORY_H
#define BD_SHELL_HISTORY_H

#include <stddef.h>

#define SHELL_LINE_MAX 128

/* Histórico de comandos (16 entradas). Linhas vazias e repetidas consecutivas são ignoradas. */
void history_add(const char *line);
size_t history_count(void);

/* age = 0 é a linha mais recente. Retorna NULL se não existir. */
const char *history_entry(size_t age);

#endif /* BD_SHELL_HISTORY_H */
