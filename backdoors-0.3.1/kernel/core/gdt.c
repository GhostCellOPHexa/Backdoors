/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>

#include "kernel/gdt.h"
#include "kernel/string.h"

/* Descritor de segmento de 8 bytes (formato Intel). */
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* Acesso: P=1, DPL, S=1 (segmento de código/dados), tipo. */
#define ACC_KERNEL_CODE 0x9A   /* P | DPL0 | S | code, exec/read */
#define ACC_KERNEL_DATA 0x92   /* P | DPL0 | S | data, read/write */
#define ACC_USER_CODE   0xFA   /* P | DPL3 | S | code, exec/read */
#define ACC_USER_DATA   0xF2   /* P | DPL3 | S | data, read/write */
/* Granularidade: G=1 (páginas de 4 KiB), D=1 (32 bits), limite alto = 0xF. */
#define GRAN_FLAT       0xCF

#define GDT_COUNT 5

extern void gdt_flush(const struct gdt_ptr *ptr);

static struct gdt_entry gdt[GDT_COUNT];
static struct gdt_ptr gdtr;

static void set_entry(int index, uint8_t access, uint8_t granularity)
{
    gdt[index].limit_low   = 0xFFFF;
    gdt[index].base_low    = 0;
    gdt[index].base_mid    = 0;
    gdt[index].access      = access;
    gdt[index].granularity = granularity;
    gdt[index].base_high   = 0;
}

void gdt_init(void)
{
    memset(gdt, 0, sizeof(gdt));                 /* índice 0: descritor nulo */
    set_entry(1, ACC_KERNEL_CODE, GRAN_FLAT);    /* 0x08 */
    set_entry(2, ACC_KERNEL_DATA, GRAN_FLAT);    /* 0x10 */
    set_entry(3, ACC_USER_CODE,   GRAN_FLAT);    /* 0x18 (usado com RPL 3 → 0x1B) */
    set_entry(4, ACC_USER_DATA,   GRAN_FLAT);    /* 0x20 (usado com RPL 3 → 0x23) */

    gdtr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdtr.base  = (uint32_t)(uintptr_t)gdt;
    gdt_flush(&gdtr);
}
