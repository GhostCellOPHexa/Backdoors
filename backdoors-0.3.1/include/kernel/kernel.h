/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_KERNEL_KERNEL_H
#define BD_KERNEL_KERNEL_H

#include <stdint.h>

/* Chamado por entry/entry.asm. Não retorna. */
__attribute__((noreturn)) void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info);

#endif /* BD_KERNEL_KERNEL_H */
