/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_MM_HEAP_H
#define BD_MM_HEAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Cria o heap do kernel com 'bytes' de memória contígua obtida do PMM. */
bool heap_init(size_t bytes);

/* Aloca 'size' bytes alinhados a 16. Retorna NULL se não houver espaço ou size == 0. */
void *kmalloc(size_t size);

/* Libera um bloco de kmalloc. Ponteiros inválidos ou liberações duplas são detectados e ignorados. */
void kfree(void *ptr);

struct heap_stats {
    size_t total;       /* bytes do heap */
    size_t used;        /* bytes de payload em blocos alocados */
    size_t free;        /* bytes de payload em blocos livres */
    size_t blocks;      /* número de blocos (alocados + livres) */
    size_t errors;      /* erros detectados (ponteiros inválidos, duplo free) */
};

void heap_get_stats(struct heap_stats *out);

/* Teste de boot: primeiro ajuste, reaproveitamento e fusão de livres. false se falhar. */
bool heap_selftest(void);

#endif /* BD_MM_HEAP_H */
