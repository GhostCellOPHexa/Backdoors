/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_SHELL_PARSER_H
#define BD_SHELL_PARSER_H

/* Divide 'line' em argumentos (altera a string). Retorna argc ou -1 em erro de sintaxe. */
int parse_line(char *line, char **argv, int max_args);

#endif /* BD_SHELL_PARSER_H */
