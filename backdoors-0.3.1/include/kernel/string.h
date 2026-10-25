/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_KERNEL_STRING_H
#define BD_KERNEL_STRING_H

#include <stddef.h>

/*
 * Funções de memória exigidas pelo ambiente freestanding: o GCC pode emitir
 * chamadas a elas mesmo quando o código não as chama diretamente.
 */
void *memset(void *dst, int value, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);

#endif /* BD_KERNEL_STRING_H */
