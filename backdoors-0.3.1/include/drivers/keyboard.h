/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_DRIVERS_KEYBOARD_H
#define BD_DRIVERS_KEYBOARD_H

#include <stdbool.h>

/* Teclas especiais (valores acima de 0xFF, fora do alcance de um char). */
#define KEY_UP    0x101
#define KEY_DOWN  0x102
#define KEY_LEFT  0x103
#define KEY_RIGHT 0x104
#define KEY_HOME  0x105
#define KEY_END   0x106
#define KEY_PGUP  0x107
#define KEY_PGDN  0x108
#define KEY_DEL   0x109
#define KEY_ESC   0x1B          /* ASCII ESC, para o editor e para interromper programas */

/* Teclado PS/2 (IRQ1), conjunto de scancodes 1 traduzido para ASCII. */
void keyboard_init(void);
bool keyboard_available(void);

/* Bloqueia até uma tecla ser pressionada. Retorna ASCII ou uma KEY_*. */
int keyboard_getkey(void);

/* Não bloqueia: retorna a próxima tecla ou -1 se a fila estiver vazia. */
int keyboard_poll(void);

#endif /* BD_DRIVERS_KEYBOARD_H */
