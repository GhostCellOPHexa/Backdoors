/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>

#include "arch/io.h"
#include "drivers/pic.h"

#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1
#define PIC_EOI    0x20

#define ICW1_INIT  0x10
#define ICW1_ICW4  0x01
#define ICW4_8086  0x01

#define PIC1_VECTOR_BASE 32
#define PIC2_VECTOR_BASE 40

/* Porta 0x80 é usada como atraso curto entre comandos ao PIC (escrita sem efeito). */
static void io_wait(void)
{
    outb(0x80, 0);
}

static uint8_t mask1 = 0xFF;
static uint8_t mask2 = 0xFF;

void pic_init(void)
{
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);  io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);  io_wait();
    outb(PIC1_DATA, PIC1_VECTOR_BASE);      io_wait();
    outb(PIC2_DATA, PIC2_VECTOR_BASE);      io_wait();
    outb(PIC1_DATA, 4);                     io_wait();   /* slave em IRQ2 */
    outb(PIC2_DATA, 2);                     io_wait();   /* identidade da cascata */
    outb(PIC1_DATA, ICW4_8086);             io_wait();
    outb(PIC2_DATA, ICW4_8086);             io_wait();

    mask1 = 0xFF;
    mask2 = 0xFF;
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

void pic_unmask(uint8_t irq)
{
    if (irq < 8) {
        mask1 &= (uint8_t)~(1u << irq);
        outb(PIC1_DATA, mask1);
    } else if (irq < 16) {
        mask2 &= (uint8_t)~(1u << (irq - 8));
        outb(PIC2_DATA, mask2);
        mask1 &= (uint8_t)~(1u << 2);            /* cascata precisa estar liberada */
        outb(PIC1_DATA, mask1);
    }
}

void pic_mask(uint8_t irq)
{
    if (irq < 8) {
        mask1 |= (uint8_t)(1u << irq);
        outb(PIC1_DATA, mask1);
    } else if (irq < 16) {
        mask2 |= (uint8_t)(1u << (irq - 8));
        outb(PIC2_DATA, mask2);
    }
}

void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);
    }
    outb(PIC1_CMD, PIC_EOI);
}
