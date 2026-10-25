/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>

#include "arch/cpu.h"
#include "arch/io.h"
#include "drivers/pic.h"
#include "drivers/timer.h"
#include "kernel/idt.h"
#include "kernel/panic.h"

#define PIT_CH0_DATA 0x40
#define PIT_CMD      0x43
#define PIT_BASE_HZ  1193182u
#define PIT_MODE3    0x36      /* canal 0, acesso lobyte/hibyte, modo 3 (onda quadrada) */

static volatile uint32_t ticks;
static uint32_t frequency;

static void timer_irq(struct regs *r)
{
    (void)r;
    ticks++;
}

void timer_init(uint32_t hz)
{
    uint32_t divisor = PIT_BASE_HZ / hz;
    /* Divisor precisa caber em 16 bits (hz >= ~18). Frequencia invalida e erro fatal. */
    if (hz == 0 || divisor == 0 || divisor > 0xFFFF) {
        kpanic("timer_init: frequencia fora do intervalo suportado pelo PIT");
    }

    frequency = hz;
    outb(PIT_CMD, PIT_MODE3);
    outb(PIT_CH0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0_DATA, (uint8_t)((divisor >> 8) & 0xFF));

    irq_register(0, timer_irq);
    pic_unmask(0);
}

uint32_t timer_ticks(void)
{
    return ticks;
}

uint32_t timer_frequency(void)
{
    return frequency;
}

void timer_wait_ticks(uint32_t n)
{
    uint32_t target = ticks + n;
    /* Comparacao com sinal: funciona mesmo com overflow do contador. */
    while ((int32_t)(ticks - target) < 0) {
        cpu_wait_for_interrupt();
    }
}
