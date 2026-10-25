/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_KERNEL_LOG_H
#define BD_KERNEL_LOG_H

#include <stddef.h>

/*
 * Saída formatada própria do kernel (sem libc).
 * Conversões suportadas: %s %c %d %u %x %X %%
 */

/* Escreve na tela VGA e na serial COM1. */
__attribute__((format(printf, 1, 2))) void kprintf(const char *fmt, ...);

/* Escreve 'n' bytes (sem terminador) na tela VGA e na serial COM1. */
void kwrite(const char *s, size_t n);

/* Escreve apenas na serial COM1 (diagnóstico). */
__attribute__((format(printf, 1, 2))) void klog(const char *fmt, ...);

#endif /* BD_KERNEL_LOG_H */
