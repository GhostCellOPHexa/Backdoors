/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>

#include "shell/history.h"

#define HISTORY_SLOTS 16

static char lines[HISTORY_SLOTS][SHELL_LINE_MAX];
static size_t count;         /* quantas entradas válidas existem (até HISTORY_SLOTS) */
static size_t newest;        /* índice do slot mais recente */

static void copy_line(char *dst, const char *src)
{
    size_t i = 0;
    while (src[i] != '\0' && i < SHELL_LINE_MAX - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int same_line(const char *a, const char *b)
{
    size_t i = 0;
    while (a[i] != '\0' && a[i] == b[i]) {
        i++;
    }
    return a[i] == b[i];
}

void history_add(const char *line)
{
    if (line[0] == '\0') {
        return;
    }
    /* Não repete a última linha. */
    if (count > 0 && same_line(lines[newest], line)) {
        return;
    }
    newest = (count == 0) ? 0 : (newest + 1) % HISTORY_SLOTS;
    copy_line(lines[newest], line);
    if (count < HISTORY_SLOTS) {
        count++;
    }
}

size_t history_count(void)
{
    return count;
}

const char *history_entry(size_t age)
{
    if (age >= count) {
        return NULL;
    }
    size_t index = (newest + HISTORY_SLOTS - age) % HISTORY_SLOTS;
    return lines[index];
}
