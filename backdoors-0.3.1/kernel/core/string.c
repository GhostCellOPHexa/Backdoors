/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>
#include <stdint.h>

#include "kernel/string.h"

/*
 * Compilado com -fno-tree-loop-distribute-patterns: sem essa opção o GCC
 * transformaria os laços abaixo em chamadas a memset/memcpy, gerando recursão.
 */

void *memset(void *dst, int value, size_t n)
{
    uint8_t *d = dst;
    uint8_t v = (uint8_t)value;
    while (n-- > 0) {
        *d++ = v;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n-- > 0) {
        *d++ = *s++;
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d == s || n == 0) {
        return dst;
    }
    if (d < s) {
        while (n-- > 0) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n-- > 0) {
            *--d = *--s;
        }
    }
    return dst;
}
