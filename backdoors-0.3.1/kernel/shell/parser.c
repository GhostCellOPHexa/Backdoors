/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>

#include "shell/parser.h"

/*
 * Divide a linha em argumentos, separados por espaços. Aspas duplas agrupam
 * um argumento com espaços (ex.: echo "a b"). Os caracteres de aspas são
 * removidos e a string é alterada no lugar.
 * Retorna o número de argumentos, ou -1 se houver aspas sem fechamento
 * ou se a linha tiver argumentos demais.
 */
int parse_line(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *src = line;
    char *dst = line;

    for (;;) {
        while (*src == ' ') {
            src++;
        }
        if (*src == '\0') {
            break;
        }
        if (argc >= max_args) {
            return -1;
        }

        argv[argc++] = dst;
        int in_quotes = 0;
        while (*src != '\0') {
            if (*src == '"') {
                in_quotes = !in_quotes;
                src++;
                continue;
            }
            if (!in_quotes && *src == ' ') {
                break;
            }
            *dst++ = *src++;
        }
        /* Consome o espaço separador antes de gravar o terminador: dst pode
         * cair exatamente na posição desse espaço e apagá-lo. */
        if (*src == ' ') {
            src++;
        }
        *dst++ = '\0';

        if (in_quotes) {
            return -1;
        }
    }

    return argc;
}
