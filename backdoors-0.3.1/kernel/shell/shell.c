/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>

#include "arch/cpu.h"
#include "drivers/keyboard.h"
#include "drivers/vga_text.h"
#include "kernel/log.h"
#include "shell/commands.h"
#include "shell/history.h"
#include "shell/parser.h"
#include "shell/script.h"
#include "shell/shell.h"

#define MAX_ARGS     16
#define TEXT_COLOR   VGA_WHITE_ON_BLACK
#define PROMPT_COLOR VGA_LIGHT_GREEN

static const char *prompt = "bd> ";

void shell_set_prompt(const char *text)
{
    if (text != NULL) {
        prompt = text;
    }
}

static void copy_str(char *dst, const char *src, size_t cap)
{
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void repeat_char(char c, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        kprintf("%c", c);
    }
}

/* Redesenha o trecho da linha a partir de 'pos' e volta o cursor para 'pos'. */
static void redraw_from(const char *buf, size_t len, size_t pos, size_t extra_blank)
{
    kwrite(buf + pos, len - pos);
    repeat_char(' ', extra_blank);
    repeat_char('\b', (len - pos) + extra_blank);
}

/* Move o cursor para a esquerda ou para a direita 'n' posições (sem apagar). */
static void move_left(size_t n)
{
    repeat_char('\b', n);
}

static void move_right(const char *buf, size_t from, size_t n)
{
    kwrite(buf + from, n);
}

/*
 * Lê uma linha com edição. Teclas: setas esquerda/direita movem o cursor,
 * Home/End vão ao início/fim, Delete e backspace apagam, setas para cima e
 * para baixo navegam no histórico. Inserção é feita na posição do cursor.
 */
static size_t readline(char *buf, size_t cap)
{
    size_t len = 0;
    size_t pos = 0;
    int browse = -1;                       /* -1: linha em edição; >=0: índice no histórico */
    char editing[SHELL_LINE_MAX];          /* rascunho guardado ao entrar no histórico */
    buf[0] = '\0';

    for (;;) {
        int key = keyboard_getkey();

        switch (key) {
        case '\n':
            move_right(buf, pos, len - pos);
            kprintf("\n");
            buf[len] = '\0';
            return len;

        case '\b':
            if (pos > 0) {
                for (size_t i = pos - 1; i + 1 < len; i++) {
                    buf[i] = buf[i + 1];
                }
                pos--;
                len--;
                buf[len] = '\0';
                move_left(1);
                redraw_from(buf, len, pos, 1);
            }
            break;

        case KEY_DEL:
            if (pos < len) {
                for (size_t i = pos; i + 1 < len; i++) {
                    buf[i] = buf[i + 1];
                }
                len--;
                buf[len] = '\0';
                redraw_from(buf, len, pos, 1);
            }
            break;

        case KEY_LEFT:
            if (pos > 0) {
                pos--;
                move_left(1);
            }
            break;

        case KEY_RIGHT:
            if (pos < len) {
                move_right(buf, pos, 1);
                pos++;
            }
            break;

        case KEY_HOME:
            move_left(pos);
            pos = 0;
            break;

        case KEY_END:
            move_right(buf, pos, len - pos);
            pos = len;
            break;

        case KEY_UP:
        case KEY_DOWN: {
            const char *src = NULL;
            if (key == KEY_UP && browse + 1 < (int)history_count()) {
                if (browse == -1) {
                    copy_str(editing, buf, sizeof(editing));
                }
                browse++;
                src = history_entry((size_t)browse);
            } else if (key == KEY_DOWN && browse >= 0) {
                browse--;
                src = (browse == -1) ? editing : history_entry((size_t)browse);
            } else {
                break;
            }
            /* Vai para o fim, apaga a linha inteira e escreve a nova. */
            move_right(buf, pos, len - pos);
            repeat_char('\b', len);
            repeat_char(' ', len);
            repeat_char('\b', len);
            copy_str(buf, src, cap);
            len = 0;
            while (buf[len] != '\0') {
                len++;
            }
            pos = len;
            kwrite(buf, len);
            break;
        }

        default:
            if (key >= 32 && key < 127 && len + 1 < cap) {
                for (size_t i = len; i > pos; i--) {
                    buf[i] = buf[i - 1];
                }
                buf[pos] = (char)key;
                len++;
                buf[len] = '\0';
                pos++;
                browse = -1;
                kprintf("%c", key);
                redraw_from(buf, len, pos, 0);
            }
            break;
        }
    }
}

void shell_run(void)
{
    char line[SHELL_LINE_MAX];
    char expanded[SHELL_LINE_MAX];
    char *argv[MAX_ARGS];

    for (;;) {
        vga_set_color(PROMPT_COLOR);
        kprintf("%s", prompt);
        vga_set_color(TEXT_COLOR);

        readline(line, sizeof(line));
        if (line[0] == '\0') {
            continue;
        }
        history_add(line);

        script_expand(line, expanded, sizeof(expanded));
        int argc = parse_line(expanded, argv, MAX_ARGS);
        if (argc < 0) {
            vga_set_color(VGA_LIGHT_RED);
            kprintf("erro de sintaxe: aspas sem fechamento ou argumentos demais\n");
            vga_set_color(TEXT_COLOR);
            continue;
        }
        if (argc == 0) {
            continue;
        }
        commands_dispatch(argc, argv);
    }
}
