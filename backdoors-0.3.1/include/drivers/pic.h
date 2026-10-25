/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_DRIVERS_PIC_H
#define BD_DRIVERS_PIC_H

#include <stdint.h>

/* PIC 8259 em cascata. IRQs 0..7 → vetores 32..39; IRQs 8..15 → vetores 40..47. */
void pic_init(void);                 /* remapeia e mascara todos os IRQs */
void pic_unmask(uint8_t irq);
void pic_mask(uint8_t irq);
void pic_send_eoi(uint8_t irq);

#endif /* BD_DRIVERS_PIC_H */
