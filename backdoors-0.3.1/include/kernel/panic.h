/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_KERNEL_PANIC_H
#define BD_KERNEL_PANIC_H

/* Falha fatal: mostra a mensagem na tela e na serial, depois para a CPU. Não retorna. */
__attribute__((noreturn)) void kpanic(const char *msg);

#endif /* BD_KERNEL_PANIC_H */
