/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_APPS_UI_H
#define BD_APPS_UI_H

#include <stddef.h>
#include <stdint.h>

/*
 * Primitivas de tela cheia para os aplicativos de texto (editor, gerenciador
 * de arquivos). Desenham direto na VGA 80x25, sem rolagem.
 */

#define UI_ATTR_NORMAL   0x07
#define UI_ATTR_INVERSE  0x70   /* barras de status */
#define UI_ATTR_DIM      0x08   /* linhas além do fim do arquivo */
#define UI_ATTR_ERROR    0x0C   /* mensagens de erro */
#define UI_ATTR_OK       0x0A   /* mensagens de sucesso */

/* Apaga a tela inteira com o atributo dado. */
void ui_clear(uint8_t attr);

/* Escreve até 'width' colunas a partir de (row, col); completa com espaços. */
void ui_text(unsigned row, unsigned col, const char *s, uint8_t attr, unsigned width);

/* Preenche a linha inteira com o atributo. */
void ui_fill_row(unsigned row, uint8_t attr);

/*
 * Pede uma linha de texto na linha 'row' (prefixo 'label'). Enter confirma
 * (retorna 1), ESC cancela (retorna 0). 'buf' recebe o texto, sem '\n'.
 */
int ui_prompt(unsigned row, const char *label, char *buf, size_t cap);

/* Espera uma tecla e a devolve (bloqueante). */
int ui_wait_key(void);

#endif /* BD_APPS_UI_H */
