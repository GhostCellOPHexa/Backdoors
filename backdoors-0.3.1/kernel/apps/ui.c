/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>
#include <stdint.h>

#include "apps/ui.h"
#include "drivers/keyboard.h"
#include "drivers/vga_text.h"

void ui_clear(uint8_t attr)
{
    for (unsigned r = 0; r < VGA_ROWS; r++) {
        ui_fill_row(r, attr);
    }
}

void ui_fill_row(unsigned row, uint8_t attr)
{
    for (unsigned c = 0; c < VGA_COLS; c++) {
        vga_put_at(row, c, ' ', attr);
    }
}

void ui_text(unsigned row, unsigned col, const char *s, uint8_t attr, unsigned width)
{
    unsigned i = 0;
    for (; i < width && col + i < VGA_COLS && s[i] != '\0'; i++) {
        vga_put_at(row, col + i, s[i], attr);
    }
    for (; i < width && col + i < VGA_COLS; i++) {
        vga_put_at(row, col + i, ' ', attr);
    }
}

static void draw_prompt(unsigned row, const char *label, const char *buf, size_t len)
{
    ui_fill_row(row, UI_ATTR_INVERSE);
    unsigned col = 0;
    for (unsigned i = 0; label[i] != '\0' && col < VGA_COLS; i++, col++) {
        vga_put_at(row, col, label[i], UI_ATTR_INVERSE);
    }
    for (size_t i = 0; i < len && col < VGA_COLS - 1; i++, col++) {
        vga_put_at(row, col, buf[i], UI_ATTR_INVERSE);
    }
    vga_set_cursor(row, col);
}

int ui_prompt(unsigned row, const char *label, char *buf, size_t cap)
{
    size_t len = 0;
    buf[0] = '\0';
    for (;;) {
        draw_prompt(row, label, buf, len);
        int key = keyboard_getkey();
        if (key == '\n') {
            return 1;
        }
        if (key == KEY_ESC) {
            return 0;
        }
        if (key == '\b') {
            if (len > 0) {
                len--;
                buf[len] = '\0';
            }
        } else if (key >= 32 && key < 127 && len + 1 < cap) {
            buf[len++] = (char)key;
            buf[len] = '\0';
        }
    }
}

int ui_wait_key(void)
{
    return keyboard_getkey();
}
