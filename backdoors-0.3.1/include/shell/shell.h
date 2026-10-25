/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_SHELL_SHELL_H
#define BD_SHELL_SHELL_H

/* Define o texto do prompt (padrão "bd> "). Vale a partir do próximo prompt. */
void shell_set_prompt(const char *prompt);

/* Laço principal do shell. Não retorna. */
__attribute__((noreturn)) void shell_run(void);

#endif /* BD_SHELL_SHELL_H */
