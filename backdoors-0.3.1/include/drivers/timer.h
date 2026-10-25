/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_DRIVERS_TIMER_H
#define BD_DRIVERS_TIMER_H

#include <stdint.h>

/* PIT canal 0 (IRQ0) em onda quadrada na frequência pedida, em Hz. */
void timer_init(uint32_t hz);

/* Frequência configurada (0 se o timer não foi inicializado). */
uint32_t timer_frequency(void);

/* Número de ticks desde timer_init. */
uint32_t timer_ticks(void);

/* Espera n ticks. Exige interrupções habilitadas. */
void timer_wait_ticks(uint32_t n);

#endif /* BD_DRIVERS_TIMER_H */
