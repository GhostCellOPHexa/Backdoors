/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/log.h"
#include "mm/heap.h"
#include "mm/pmm.h"

/*
 * Alocador de lista de blocos com primeiro ajuste e fusão de livres vizinhos.
 * Cada bloco tem um cabeçalho de 32 bytes (alinhado a 16) com um magic para
 * detectar ponteiros inválidos e corrupção. Os blocos são contíguos na região
 * do heap: o próximo bloco começa logo após o payload deste.
 */

#define HEAP_MAGIC_LIVE  0xBD5EED01u
#define HEAP_MAGIC_FREE  0xBD5EED00u
#define ALIGN            16u
#define HDR_SIZE         32u
#define MIN_SPLIT        (HDR_SIZE + ALIGN)

struct block {
    uint32_t magic;
    uint32_t size;          /* bytes de payload */
    uint32_t is_free;
    struct block *next;     /* próximo bloco na região */
    struct block *prev;     /* bloco anterior na região */
    uint32_t pad[3];        /* completa o cabeçalho para 32 bytes */
} __attribute__((aligned(16)));

_Static_assert(sizeof(struct block) == HDR_SIZE, "cabeçalho do heap deve ter 32 bytes");

static struct block *head;
static uint8_t *heap_base;
static size_t heap_bytes;
static size_t error_count;

static size_t align_up(size_t n)
{
    return (n + ALIGN - 1) & ~(size_t)(ALIGN - 1);
}

bool heap_init(size_t bytes)
{
    uint32_t frames = (uint32_t)((bytes + PMM_FRAME_SIZE - 1) / PMM_FRAME_SIZE);
    uint32_t phys = pmm_alloc_contig(frames);
    if (phys == 0) {
        return false;
    }

    heap_base = (uint8_t *)(uintptr_t)phys;
    heap_bytes = (size_t)frames * PMM_FRAME_SIZE;

    head = (struct block *)heap_base;
    head->magic = HEAP_MAGIC_FREE;
    head->size = (uint32_t)(heap_bytes - HDR_SIZE);
    head->is_free = 1;
    head->next = NULL;
    head->prev = NULL;
    return true;
}

static bool block_valid(const struct block *b)
{
    return b != NULL && (b->magic == HEAP_MAGIC_LIVE || b->magic == HEAP_MAGIC_FREE);
}

/* Divide 'b' se sobrar espaço suficiente para outro bloco útil. */
static void split(struct block *b, size_t want)
{
    if (b->size < want + MIN_SPLIT) {
        return;
    }
    struct block *rest = (struct block *)((uint8_t *)b + HDR_SIZE + want);
    rest->magic = HEAP_MAGIC_FREE;
    rest->size = (uint32_t)(b->size - want - HDR_SIZE);
    rest->is_free = 1;
    rest->next = b->next;
    rest->prev = b;
    if (b->next != NULL) {
        b->next->prev = rest;
    }
    b->next = rest;
    b->size = (uint32_t)want;
}

void *kmalloc(size_t size)
{
    if (size == 0 || head == NULL) {
        return NULL;
    }
    size_t want = align_up(size);
    for (struct block *b = head; b != NULL; b = b->next) {
        if (!block_valid(b)) {
            klog("[heap] corrupção detectada em %x\n", (uint32_t)(uintptr_t)b);
            error_count++;
            return NULL;
        }
        if (b->is_free && b->size >= want) {
            split(b, want);
            b->is_free = 0;
            b->magic = HEAP_MAGIC_LIVE;
            return (uint8_t *)b + HDR_SIZE;
        }
    }
    return NULL;
}

/* Funde 'b' com o vizinho seguinte se ambos estiverem livres. */
static void merge_with_next(struct block *b)
{
    struct block *n = b->next;
    if (n == NULL || !n->is_free) {
        return;
    }
    b->size += (uint32_t)(HDR_SIZE + n->size);
    b->next = n->next;
    if (n->next != NULL) {
        n->next->prev = b;
    }
    n->magic = 0;
}

void kfree(void *ptr)
{
    if (ptr == NULL) {
        return;
    }
    struct block *b = (struct block *)((uint8_t *)ptr - HDR_SIZE);
    if (b->magic == HEAP_MAGIC_FREE) {
        klog("[heap] erro: liberação dupla em %x\n", (uint32_t)(uintptr_t)ptr);
        error_count++;
        return;
    }
    if (b->magic != HEAP_MAGIC_LIVE) {
        klog("[heap] erro: ponteiro inválido em %x\n", (uint32_t)(uintptr_t)ptr);
        error_count++;
        return;
    }

    b->is_free = 1;
    b->magic = HEAP_MAGIC_FREE;
    merge_with_next(b);
    if (b->prev != NULL && b->prev->is_free) {
        merge_with_next(b->prev);
    }
}

void heap_get_stats(struct heap_stats *out)
{
    out->total = heap_bytes;
    out->used = 0;
    out->free = 0;
    out->blocks = 0;
    out->errors = error_count;
    for (const struct block *b = head; b != NULL; b = b->next) {
        out->blocks++;
        if (b->is_free) {
            out->free += b->size;
        } else {
            out->used += b->size;
        }
    }
}

bool heap_selftest(void)
{
    /* Três blocos, libera o do meio e aloca um menor: deve reaproveitar o espaço. */
    void *a = kmalloc(100);
    void *b = kmalloc(200);
    void *c = kmalloc(300);
    if (a == NULL || b == NULL || c == NULL || a == b || b == c) {
        return false;
    }

    kfree(b);
    void *d = kmalloc(150);
    if (d != b) {
        return false;
    }

    /* Liberando tudo, os blocos vizinhos devem fundir até restar um único bloco livre. */
    kfree(a);
    kfree(c);
    kfree(d);

    struct heap_stats s;
    heap_get_stats(&s);
    return s.used == 0 && s.blocks == 1 && s.free == s.total - HDR_SIZE;
}
