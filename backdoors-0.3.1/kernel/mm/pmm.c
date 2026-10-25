/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/log.h"
#include "kernel/multiboot.h"
#include "kernel/panic.h"
#include "kernel/string.h"
#include "mm/pmm.h"

/* Limites do bitmap: 2^20 frames x 4 KiB = 4 GiB (cobre todo o espaço de 32 bits). */
#define MAX_FRAMES  (1u << 20)
#define BITS_PER_WORD 32u

extern const uint8_t __kernel_start[];
extern const uint8_t __kernel_end[];

static uint32_t bitmap[MAX_FRAMES / BITS_PER_WORD];   /* 1 = ocupado */
static uint32_t total_frames;     /* frames de RAM disponível (informados pelo bootloader) */
static uint32_t free_frames;
static uint32_t search_hint;      /* posição onde a busca recomeça */

static bool frame_used(uint32_t f)
{
    return (bitmap[f / BITS_PER_WORD] >> (f % BITS_PER_WORD)) & 1u;
}

static void mark_used(uint32_t f)
{
    if (!frame_used(f)) {
        bitmap[f / BITS_PER_WORD] |= 1u << (f % BITS_PER_WORD);
        free_frames--;
    }
}

static void mark_free(uint32_t f)
{
    if (frame_used(f)) {
        bitmap[f / BITS_PER_WORD] &= ~(1u << (f % BITS_PER_WORD));
        free_frames++;
    }
}

/* Converte [start, end) em frames inteiros: início arredondado para cima, fim para baixo. */
static void range_to_frames(uint64_t start, uint64_t end, uint32_t *first, uint32_t *last)
{
    uint64_t f0 = (start + PMM_FRAME_SIZE - 1) >> 12;
    uint64_t f1 = end >> 12;
    if (f1 > MAX_FRAMES) {
        f1 = MAX_FRAMES;
    }
    if (f0 > MAX_FRAMES) {
        f0 = MAX_FRAMES;
    }
    *first = (uint32_t)f0;
    *last  = (uint32_t)f1;
}

void pmm_reserve_range(uint32_t start, uint32_t end)
{
    uint32_t lo = start >> 12;
    uint32_t hi = (end + PMM_FRAME_SIZE - 1) >> 12;   /* arredonda o fim para cima */
    if (hi > MAX_FRAMES) {
        hi = MAX_FRAMES;
    }
    for (uint32_t f = lo; f < hi; f++) {
        mark_used(f);
    }
}

void pmm_init(const struct multiboot_info *mbi)
{
    /* Começa tudo ocupado; só liberamos o que o bootloader declarar como disponível. */
    memset(bitmap, 0xFF, sizeof(bitmap));
    free_frames = 0;
    total_frames = 0;
    search_hint = 0;

    if (mbi->flags & MULTIBOOT_INFO_MMAP) {
        const uint8_t *p = (const uint8_t *)(uintptr_t)mbi->mmap_addr;
        const uint8_t *end = p + mbi->mmap_length;
        while (p < end) {
            const struct multiboot_mmap_entry *e = (const struct multiboot_mmap_entry *)p;
            if (e->type == MULTIBOOT_MEMORY_AVAILABLE) {
                uint32_t first, last;
                range_to_frames(e->base_addr, e->base_addr + e->length, &first, &last);
                for (uint32_t f = first; f < last; f++) {
                    if (frame_used(f)) {
                        mark_free(f);
                        total_frames++;
                    }
                }
            }
            p += e->size + sizeof(e->size);
        }
    } else if (mbi->flags & MULTIBOOT_INFO_MEMORY) {
        /* Sem mapa completo: usa só a faixa contígua acima de 1 MiB. */
        uint64_t end = 0x100000ull + (uint64_t)mbi->mem_upper * 1024ull;
        uint32_t first, last;
        range_to_frames(0x100000ull, end, &first, &last);
        for (uint32_t f = first; f < last; f++) {
            mark_free(f);
            total_frames++;
        }
        klog("[mm] aviso: sem mapa de memória; usando mem_upper apenas\n");
    } else {
        kpanic("bootloader não informou memória (sem mmap nem mem_upper)");
    }

    /* Reservas obrigatórias. */
    pmm_reserve_range(0, 0x100000);                                  /* baixa memória, BIOS, VGA */
    pmm_reserve_range((uint32_t)(uintptr_t)__kernel_start,
                      (uint32_t)(uintptr_t)__kernel_end);            /* imagem do kernel e .bss */
    pmm_reserve_range((uint32_t)(uintptr_t)mbi,
                      (uint32_t)(uintptr_t)mbi + sizeof(*mbi));      /* info Multiboot */
    if (mbi->flags & MULTIBOOT_INFO_MMAP) {
        pmm_reserve_range(mbi->mmap_addr, mbi->mmap_addr + mbi->mmap_length);
    }
    if (mbi->flags & MULTIBOOT_INFO_MODS) {
        const struct multiboot_module *mods =
            (const struct multiboot_module *)(uintptr_t)mbi->mods_addr;
        for (uint32_t i = 0; i < mbi->mods_count; i++) {
            pmm_reserve_range(mods[i].mod_start, mods[i].mod_end);
        }
    }
}

uint32_t pmm_alloc_frame(void)
{
    for (uint32_t n = 0; n < MAX_FRAMES; n++) {
        uint32_t f = (search_hint + n) % MAX_FRAMES;
        if (!frame_used(f)) {
            mark_used(f);
            search_hint = f + 1;
            return f << 12;
        }
    }
    return 0;
}

uint32_t pmm_alloc_contig(uint32_t count)
{
    if (count == 0 || count > MAX_FRAMES) {
        return 0;
    }
    uint32_t run = 0;
    for (uint32_t f = 0; f < MAX_FRAMES; f++) {
        run = frame_used(f) ? 0 : run + 1;
        if (run == count) {
            uint32_t first = f + 1 - count;
            for (uint32_t g = first; g <= f; g++) {
                mark_used(g);
            }
            return first << 12;
        }
    }
    return 0;
}

bool pmm_free_frame(uint32_t addr)
{
    if ((addr & (PMM_FRAME_SIZE - 1)) != 0) {
        klog("[mm] erro: free de endereço não alinhado 0x%x\n", addr);
        return false;
    }
    uint32_t f = addr >> 12;
    if (f >= MAX_FRAMES || !frame_used(f)) {
        klog("[mm] erro: free de frame não alocado 0x%x\n", addr);
        return false;
    }
    mark_free(f);
    if (f < search_hint) {
        search_hint = f;
    }
    return true;
}

uint32_t pmm_total_frames(void)
{
    return total_frames;
}

uint32_t pmm_free_frames(void)
{
    return free_frames;
}
