/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_SHELL_SCRIPT_H
#define BD_SHELL_SCRIPT_H

#include <stddef.h>

/*
 * Linguagem de script do shell (versão 0.3.1): um arquivo de texto com um
 * comando por linha. Linhas vazias e linhas que começam com '#' são ignoradas.
 * Variáveis: "set NOME VALOR" e $NOME nas linhas (expandido antes de executar).
 * Executa-se com "sh ARQUIVO". Ainda não há if/for no shell: use programas
 * .int ou Python para lógica.
 */

/* Define ou troca uma variável. Retorna 0, ou -1 se a tabela estiver cheia ou o nome for inválido. */
int script_set(const char *name, const char *value);

/* Valor da variável, ou "" se não existir. */
const char *script_get(const char *name);

/* Copia 'in' para 'out' substituindo $NOME pelo valor da variável. */
void script_expand(const char *in, char *out, size_t cap);

/* Executa o arquivo como script. Retorna 0, ou -1 se não for possível ler. */
int script_run_file(const char *path);

#endif /* BD_SHELL_SCRIPT_H */
