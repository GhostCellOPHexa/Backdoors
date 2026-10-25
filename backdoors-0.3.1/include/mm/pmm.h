/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_MM_PMM_H
#define BD_MM_PMM_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel/multiboot.h"

#define PMM_FRAME_SIZE 4096u

/*
 * Gerenciador de frames físicos: bitmap de 4 GiB (1 bit por frame de 4 KiB).
 * Só usa RAM marcada como disponível no mapa de memória do bootloader.
 * Reservados: memória abaixo de 1 MiB, imagem do kernel, estrutura Multiboot,
 * mapa de memória e módulos carregados pelo bootloader.
 */

/* Inicializa a partir do mapa Multiboot. kpanic se não houver informação de memória. */
void pmm_init(const struct multiboot_info *mbi);

/* Marca [start, end) como ocupado (ex.: módulos carregados depois da inicialização). */
void pmm_reserve_range(uint32_t start, uint32_t end);

/* Aloca um frame. Retorna endereço físico ou 0 se não houver memória. */
uint32_t pmm_alloc_frame(void);

/* Aloca 'count' frames contíguos. Retorna endereço do primeiro ou 0. */
uint32_t pmm_alloc_contig(uint32_t count);

/* Libera um frame. Retorna false (e registra erro) se o frame não estava alocado. */
bool pmm_free_frame(uint32_t addr);

/* Estatísticas, em frames de 4 KiB. */
uint32_t pmm_total_frames(void);   /* RAM disponível informada pelo bootloader */
uint32_t pmm_free_frames(void);

#endif /* BD_MM_PMM_H */
