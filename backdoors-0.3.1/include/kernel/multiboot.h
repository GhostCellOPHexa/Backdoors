/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_KERNEL_MULTIBOOT_H
#define BD_KERNEL_MULTIBOOT_H

#include <stdint.h>

/* Valor deixado em EAX pelo bootloader que segue a especificação Multiboot 1. */
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002u

/* Bits de 'flags' em struct multiboot_info. */
#define MULTIBOOT_INFO_MEMORY   0x001   /* mem_lower e mem_upper válidos */
#define MULTIBOOT_INFO_MODS     0x008   /* mods_count e mods_addr válidos */
#define MULTIBOOT_INFO_MMAP     0x040   /* mmap_length e mmap_addr válidos */

/* Estrutura de informações passada pelo bootloader (offsets da especificação). */
struct multiboot_info {
    uint32_t flags;          /* 0  */
    uint32_t mem_lower;      /* 4  KiB abaixo de 1 MiB */
    uint32_t mem_upper;      /* 8  KiB acima de 1 MiB */
    uint32_t boot_device;    /* 12 */
    uint32_t cmdline;        /* 16 */
    uint32_t mods_count;     /* 20 */
    uint32_t mods_addr;      /* 24 */
    uint32_t syms[4];        /* 28..43 */
    uint32_t mmap_length;    /* 44 bytes do array de entradas */
    uint32_t mmap_addr;      /* 48 endereço físico do array */
} __attribute__((packed));

/* Entrada do mapa de memória. 'size' não inclui o próprio campo. */
struct multiboot_mmap_entry {
    uint32_t size;
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
} __attribute__((packed));

#define MULTIBOOT_MEMORY_AVAILABLE 1

/* Módulo carregado pelo GRUB (ex.: initramfs). */
struct multiboot_module {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t string;
    uint32_t reserved;
} __attribute__((packed));

#endif /* BD_KERNEL_MULTIBOOT_H */
