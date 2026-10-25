/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_DRIVERS_VGA_TEXT_H
#define BD_DRIVERS_VGA_TEXT_H

#include <stdint.h>

/* Modo texto VGA 80x25 em 0xB8000. Cada célula: caractere (byte) + atributo (byte). */

#define VGA_WHITE_ON_BLACK 0x07
#define VGA_WHITE_ON_RED   0x4F

void vga_init(void);
void vga_clear(void);
void vga_set_color(uint8_t attr);
void vga_putc(char c);

/* Telas cheias (editor, gerenciador de arquivos): não mexem no cursor de texto. */
#define VGA_COLS 80
#define VGA_ROWS 25
void vga_put_at(unsigned row, unsigned col, char c, uint8_t attr);
void vga_set_cursor(unsigned row, unsigned col);
void vga_show_cursor(int visible);

/* Cores extras de terminal (atributo = cor do texto | fundo << 4). */
#define VGA_LIGHT_GREEN   0x0A
#define VGA_LIGHT_RED     0x0C
#define VGA_LIGHT_CYAN    0x0B
#define VGA_YELLOW        0x0E

#endif /* BD_DRIVERS_VGA_TEXT_H */
