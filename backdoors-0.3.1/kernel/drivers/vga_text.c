/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>
#include <stdint.h>

#include "arch/io.h"
#include "drivers/vga_text.h"

#define VGA_MEM   ((volatile uint16_t *)0xB8000)

/* Portas do controlador CRT, usadas para mover o cursor de hardware. */
#define CRTC_ADDR 0x3D4
#define CRTC_DATA 0x3D5

static size_t cur_row;
static size_t cur_col;
static uint8_t cur_attr = VGA_WHITE_ON_BLACK;

static void blank_row(size_t row)
{
    uint16_t blank = (uint16_t)' ' | ((uint16_t)cur_attr << 8);
    for (size_t x = 0; x < VGA_COLS; x++) {
        VGA_MEM[row * VGA_COLS + x] = blank;
    }
}

static void scroll_up(void)
{
    for (size_t y = 1; y < VGA_ROWS; y++) {
        for (size_t x = 0; x < VGA_COLS; x++) {
            VGA_MEM[(y - 1) * VGA_COLS + x] = VGA_MEM[y * VGA_COLS + x];
        }
    }
    blank_row(VGA_ROWS - 1);
}

static void update_cursor(void)
{
    uint16_t pos = (uint16_t)(cur_row * VGA_COLS + cur_col);
    outb(CRTC_ADDR, 0x0F);
    outb(CRTC_DATA, (uint8_t)(pos & 0xFF));
    outb(CRTC_ADDR, 0x0E);
    outb(CRTC_DATA, (uint8_t)((pos >> 8) & 0xFF));
}

static void newline(void)
{
    cur_col = 0;
    cur_row++;
    if (cur_row == VGA_ROWS) {
        scroll_up();
        cur_row = VGA_ROWS - 1;
    }
}

void vga_init(void)
{
    cur_row = 0;
    cur_col = 0;
    cur_attr = VGA_WHITE_ON_BLACK;
    for (size_t y = 0; y < VGA_ROWS; y++) {
        blank_row(y);
    }
    update_cursor();
}

void vga_clear(void)
{
    for (size_t y = 0; y < VGA_ROWS; y++) {
        blank_row(y);
    }
    cur_row = 0;
    cur_col = 0;
    update_cursor();
}

void vga_set_color(uint8_t attr)
{
    cur_attr = attr;
}

void vga_putc(char c)
{
    switch (c) {
    case '\n':
        newline();
        break;
    case '\r':
        cur_col = 0;
        break;
    case '\b':
        if (cur_col > 0) {
            cur_col--;
        }
        break;
    default:
        VGA_MEM[cur_row * VGA_COLS + cur_col] =
            (uint16_t)(uint8_t)c | ((uint16_t)cur_attr << 8);
        cur_col++;
        if (cur_col == VGA_COLS) {
            newline();
        }
        break;
    }
    update_cursor();
}

void vga_put_at(unsigned row, unsigned col, char c, uint8_t attr)
{
    if (row >= VGA_ROWS || col >= VGA_COLS) {
        return;
    }
    VGA_MEM[row * VGA_COLS + col] = (uint16_t)(uint8_t)c | ((uint16_t)attr << 8);
}

void vga_set_cursor(unsigned row, unsigned col)
{
    if (row >= VGA_ROWS || col >= VGA_COLS) {
        return;
    }
    uint16_t pos = (uint16_t)(row * VGA_COLS + col);
    outb(CRTC_ADDR, 0x0F);
    outb(CRTC_DATA, (uint8_t)(pos & 0xFF));
    outb(CRTC_ADDR, 0x0E);
    outb(CRTC_DATA, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_show_cursor(int visible)
{
    /* Bit 5 do registrador 0x0A do CRTC desliga o cursor de hardware. */
    outb(CRTC_ADDR, 0x0A);
    uint8_t v = inb(CRTC_DATA);
    if (visible) {
        v = (uint8_t)(v & ~0x20);
    } else {
        v = (uint8_t)(v | 0x20);
    }
    outb(CRTC_DATA, v);
}
